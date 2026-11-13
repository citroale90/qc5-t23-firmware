#ifndef QC5_BOOT_POLICY_GENERATED_H
#define QC5_BOOT_POLICY_GENERATED_H
struct qenv_pair {const char *name,*value;};
static const struct qenv_pair qenv_policy[]={
{"verify","yes"},
{"bootcmd","run q73over q73skip; run q73arm q73deny; run q73arm q73load q73check q73exec; run q73normal"},
{"q73over","fatload mmc 0 80d60000 /Q73BOOT.IMG 1 2f10"},
{"q73skip","setenv q73load run q73disabled"},
{"q73arm","fatload mmc 0 80d60000 /Q73ARM.BIN 1"},
{"q73deny","setenv q73normal run q73disabled"},
{"q73load","fatload mmc 0 80600000 /Q73BOOT.IMG 2f10"},
{"q73check","run q73size q73want q73cmp q73hword q73hcmp q73dword q73dcmp q73validate"},
{"q73size","mw.l 80d60010 ${filesize} 1"},
{"q73want","mw.l 80d60014 2f10 1"},
{"q73cmp","cmp.l 80d60010 80d60014 1"},
{"q73hword","mw.l 80d60014 31b1b953 1"},
{"q73hcmp","cmp.l 80600004 80d60014 1"},
{"q73dword","mw.l 80d60014 fe9d2e99 1"},
{"q73dcmp","cmp.l 80600018 80d60014 1"},
{"q73validate","bootm start 80600000"},
{"q73exec","bootm 80600000"},
{"q73normal","run q73probe q73kernel q73linux"},
{"q73probe","sf probe"},
{"q73kernel","sf read 80600000 40000 200000"},
{"q73linux","bootm 80600000"},
{0,0}};
static const struct qenv_pair qenv_factory[]={
{"bootargs","console=ttyS1,115200n8 quiet mem=25088K@0x0 rmem=7680K@25088K init=/linuxrc rootfstype=squashfs root=/dev/mtdblock2 rw mtdparts=jz_sfc:256k(boot),1376k(kernel),3456k(root),2656k(system),192k(www),256k(param),8m@0(all)"},
{"bootcmd","sf probe;sf read 0x80600000 0x40000 0x200000; bootm 0x80600000"},
{"bootdelay","1"},
{"baudrate","115200"},
{"loads_echo","1"},
{"ethaddr","00:d0:d0:00:95:27"},
{"ipaddr","192.168.1.126"},
{"serverip","192.168.1.101"},
{"b0_crc","0x5670d044"},
{"gatewayip","192.168.1.1"},
{"netmask","255.255.255.0"},
{0,0}};
static const char *const qenv_legacy[]={"kact","karm","karmover","karmword","kbackup","kblock","kcompare","kcontinue","kdisable","ke0","ke1","ke2","kguard","kh1","kh3","kh6","kh9","kha","kheader","kimageover","kload","knormal","kproof","kread","kready","krepair","kresume","ksize","kskip","kstamp","kstart","kverified","kwrite","q73cc","q73crc","q73cw","qarm","qarmexpected","qarmok","qarmover","qarmsize","qarmword","qboot","qchecked","qconfirm","qdisable","qe0","qe1","qe2","qe3","qe4","qerase","qexpected","qh0","qh1","qh3","qh4","qh5","qh6","qh7","qh8","qh9","qha","qhb","qheader","qhzero","qkernel","qlinux","qload","qloadover","qprepare","qprobe","qproof","qread","qrestore","qsize","qsizeok","qskipload","qstamp","qunlock","qvalidate","qwrite","rnormal","rs0","rs1","rs3","rs4","rs5","rs6","rs7","rscript","rsh","rssource","rsv","rsz","veepai_vuid",0};
#endif
