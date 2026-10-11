#!/usr/bin/env python3
"""Build Thor-only transport modules from pinned GPL Qualcomm sources."""
import argparse
import hashlib
from pathlib import Path
import re
import subprocess
import urllib.request

REV = 'c462ef8ffab7a58e035ee04705b16cdfced494b1'
BASE = f'https://raw.githubusercontent.com/OnePlusOSS/android_kernel_oneplus_sm8550/{REV}/'


def cut(text: str, begin: str, end: str) -> str:
    start = text.index(begin)
    return text[:start] + text[text.index(end, start):]


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', required=True, type=Path)
    parser.add_argument('--build', required=True, type=Path)
    parser.add_argument('--output', required=True, type=Path)
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    (out / 'include/linux/mailbox').mkdir(parents=True, exist_ok=True)
    records = [f'SOURCE_REPOSITORY={BASE}', f'SOURCE_COMMIT={REV}']

    def fetch(path: str) -> str:
        with urllib.request.urlopen(BASE + path, timeout=120) as response:
            data = response.read()
        records.append(f'SHA256 {hashlib.sha256(data).hexdigest()} {path}')
        return data.decode()

    (out / 'include/linux/mailbox/qmp.h').write_text(fetch('include/linux/mailbox/qmp.h'))
    qmp = fetch('drivers/mailbox/msm_qmp.c')
    qmp = qmp.replace('#include <linux/ipc_logging.h>', '').replace('#include <linux/soc/qcom/qcom_aoss.h>', '')
    qmp = cut(qmp, '#define QMP_IPC_LOG_PAGE_CNT', '#ifdef CONFIG_QMP_DEBUGFS_CLIENT')
    qmp = qmp.replace('#ifdef CONFIG_QMP_DEBUGFS_CLIENT', '#define QMP_INFO(ctxt, ...) do { } while (0)\n#define QMP_ERR(ctxt, ...) pr_err_ratelimited(__VA_ARGS__)\n#ifdef CONFIG_QMP_DEBUGFS_CLIENT', 1)
    qmp = cut(qmp, 'void qmp_rx_callback(', '/**\n * init_mcore_state')
    qmp = cut(qmp, 'static int qmp_shim_startup(', '/**\n * qmp_mbox_init')
    qmp = cut(qmp, 'static int qmp_shim_init(', 'static int qmp_smem_init(')
    qmp = qmp.replace('if (of_parse_phandle(edge_node, "qcom,qmp", 0))\n\t\treturn qmp_shim_init(pdev, mdev);', '')
    qmp = re.sub(r'mdev->ilc = ipc_log_context_create\([^;]+;', 'mdev->ilc = NULL;', qmp)
    qmp = qmp.replace('"qcom,qmp-mbox"', '"aynthor,tme-qmp"')
    qmp = qmp.replace('if (PTR_ERR(mdev->mbox_chan) != -ENODEV)', 'if (PTR_ERR(mdev->mbox_chan) != -ENODEV &&\n\t\t    PTR_ERR(mdev->mbox_chan) != -EBUSY)')
    qmp = qmp.replace('static int qmp_mbox_remove(', 'static void qmp_mbox_remove(')
    start = qmp.index('static void qmp_mbox_remove(')
    end = qmp.index('static int qmp_mbox_probe(', start)
    removal = qmp[start:end].replace('return 0;', 'return;')
    removal = removal.replace('mbox_controller_unregister(&mbox->ctrl);', 'cancel_delayed_work_sync(&mbox->dwork);\n\t\tmbox_controller_unregister(&mbox->ctrl);')
    removal = removal.replace('\treturn;', '\tif (mdev->mbox_chan) mbox_free_channel(mdev->mbox_chan);\n\treturn;')
    (out / 'aynthor_qmp.c').write_text(qmp[:start] + removal + qmp[end:])
    client = fetch('drivers/soc/qcom/tmecom/tmecom.c')
    client = cut(client, '#if IS_ENABLED(CONFIG_DEBUG_FS)', 'static struct tmecom *tmedev;')
    client = cut(client, '#if IS_ENABLED(CONFIG_DEBUG_FS)', 'static void tmecom_receive_message(')
    client = re.sub(r'#if IS_ENABLED\(CONFIG_DEBUG_FS\).*?#endif[^\n]*', '', client, flags=re.S)
    client = re.sub(r'print_hex_dump_bytes\([^;]+;', '', client)
    client = client.replace('tdev->cl.tx_tout);', 'msecs_to_jiffies(tdev->cl.tx_tout));')
    client = client.replace('if (!time_left) {', 'if (time_left < 0) { ret = time_left; goto err_exit; }\n\tif (!time_left) {')
    client = client.replace('*respsize = tmecom_decode(tdev, respbuf);', 'if (tdev->pkt.size - TMECOM_RX_HDR_SIZE > *respsize) {\n\t\tret = -EMSGSIZE; goto err_exit;\n\t}\n\t*respsize = tmecom_decode(tdev, respbuf);')
    client = client.replace('static int tmecom_remove(', 'static void tmecom_remove(')
    start = client.index('static void tmecom_remove(')
    end = client.index('static const struct of_device_id', start)
    removal = client[start:end].replace('return 0;', 'return;').replace('struct tmecom *tdev = platform_get_drvdata(pdev);', 'struct tmecom *tdev = platform_get_drvdata(pdev);\n\ttmedev = NULL;')
    client = client[:start] + removal + client[end:]
    client = client.replace('err:\n\tmbox_free_channel(tdev->chan);\n\treturn -ENOMEM;', '')
    (out / 'tme_client.c').write_text(client)
    for source, target in [('drivers/soc/qcom/tmecom/tmecom.h', 'tmecom.h'), ('drivers/soc/qcom/tmecom/tme_hwkm_master_intf.h', 'tme_hwkm_master_intf.h'), ('include/linux/tme_hwkm_master_defs.h', 'include/linux/tme_hwkm_master_defs.h')]:
        (out / target).write_text(fetch(source))
    master = fetch('drivers/soc/qcom/tmecom/tme_hwkm_master.c')
    start = master.index('uint32_t tme_hwkm_master_broadcast_transportkey(')
    end = master.index('EXPORT_SYMBOL(tme_hwkm_master_broadcast_transportkey);', start)
    prefix = master[:master.index('#define TME_MSG_CBOR_TAG_HWKM')]
    helper = '''#define TME_BORADCAST_KEY_CBOR_TAG 0x442F01D9
static uint32_t update_ext_err(struct tme_ext_err_info *err_info,
                              struct tme_response_sts *result)
{
    err_info->tme_err_status = result->tme_err_status;
    err_info->seq_err_status = result->seq_err_status;
    err_info->seq_kp_err_status0 = result->seq_kp_err_status0;
    err_info->seq_kp_err_status1 = result->seq_kp_err_status1;
    err_info->seq_rsp_status = result->seq_rsp_status;
    return result->tme_err_status || result->seq_err_status ||
           result->seq_kp_err_status0 || result->seq_kp_err_status1;
}
'''
    wrapper = '''
int aynthor_tme_broadcast_transport_key(void)
{
    struct tme_ext_err_info error = {0};
    return (int)tme_hwkm_master_broadcast_transportkey(&error);
}
EXPORT_SYMBOL_GPL(aynthor_tme_broadcast_transport_key);
'''
    master = (prefix + helper + master[start:end] + wrapper).replace('sizeof(response));', 'sizeof(*response));').replace('invalid length: %u, %u', 'invalid length: %zu, %zu')
    (out / 'tme_broadcast.c').write_text(master)
    payload = Path(__file__).resolve().parent.parent / 'packages/thorch-tme-modules/src'
    (out / 'tme_overlay_loader.c').write_bytes((payload / 'tme_overlay_loader.c').read_bytes())
    (out / 'Makefile').write_text('obj-m += aynthor_qmp.o tme_overlay_loader.o aynthor_tme.o\naynthor_tme-y := tme_client.o tme_broadcast.o\nccflags-y += -I$(src)/include\n')
    subprocess.run(['dtc', '-@', '-I', 'dts', '-O', 'dtb', '-o', str(out / 'tme-overlay.dtbo'), str(payload / 'tme-overlay.dts')], check=True)
    subprocess.run(['make', '-C', str(args.source.resolve()), f'O={args.build.resolve()}', 'ARCH=arm64', f'M={out}', 'modules'], check=True)
    (out / 'PROVENANCE').write_text('\n'.join(records) + '\n')


if __name__ == '__main__':
    main()
