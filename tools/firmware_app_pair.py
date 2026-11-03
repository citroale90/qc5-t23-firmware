"""SYSTEM+WWW updates which preserve the original ROOT SD recovery loader.

These functions only inspect bytes or produce RAM values; they never flash.
"""
import struct
from qc5_profile import BOARD,PARTITIONS,LAYOUT,ERASE,HEADER_SIZE,sha,encode_text,BOOT_CODE_SIZE,BOOT_CODE_SHA,squash_check

SYSTEM_OFFSET,SYSTEM_SIZE=PARTITIONS[3][1:3]
WWW_OFFSET,WWW_SIZE=PARTITIONS[4][1:3]
PAYLOAD_SIZE=SYSTEM_SIZE+WWW_SIZE
MAGIC=b'QC5UPD3\0'

def create(system,www,release,kind='modified',source_system=None,source_www=None):
    squash_check(system,SYSTEM_SIZE);squash_check(www,WWW_SIZE)
    if kind not in('modified','stock'):raise ValueError('Tipo non valido')
    if kind=='stock'and(sha(system).hex()!=PARTITIONS[3][3]or sha(www).hex()!=PARTITIONS[4][3]):
        raise ValueError('Ripristino diverso dalle applicazioni originali verificate')
    if(source_system is None)!=(source_www is None):raise ValueError('Specificare entrambe le immagini di origine')
    if source_system is not None:squash_check(source_system,SYSTEM_SIZE);squash_check(source_www,WWW_SIZE)
    h=bytearray(HEADER_SIZE);h[:8]=MAGIC
    struct.pack_into('<6I',h,8,HEADER_SIZE,PAYLOAD_SIZE,SYSTEM_OFFSET,ERASE,3,1 if kind=='modified'else 2)
    h[32:96]=BOARD.encode().ljust(64,b'\0');h[96:160]=encode_text(release,64)
    for offset,value in [(160,sha(LAYOUT.encode())),(192,bytes.fromhex(PARTITIONS[1][3])),
        (224,bytes.fromhex(PARTITIONS[2][3])),(256,bytes.fromhex(PARTITIONS[4][3])),
        (288,sha(system)),(320,sha(www)),(352,bytes.fromhex(PARTITIONS[3][3])),
        (384,bytes.fromhex(PARTITIONS[4][3])),
        (416,sha(source_system)if source_system is not None else bytes.fromhex(PARTITIONS[3][3])),
        (448,sha(source_www)if source_www is not None else bytes.fromhex(PARTITIONS[4][3]))]:h[offset:offset+32]=value
    h[480:512]=sha(h[:480]);return bytes(h)+system+www

def check(package):
    if len(package)!=HEADER_SIZE+PAYLOAD_SIZE:raise ValueError('Dimensione pacchetto non valida')
    h=package[:HEADER_SIZE];system=package[HEADER_SIZE:HEADER_SIZE+SYSTEM_SIZE];www=package[HEADER_SIZE+SYSTEM_SIZE:]
    if h[:8]!=MAGIC or struct.unpack_from('<5I',h,8)!=(HEADER_SIZE,PAYLOAD_SIZE,SYSTEM_OFFSET,ERASE,3):
        raise ValueError('Formato o destinazione non validi')
    kind=struct.unpack_from('<I',h,28)[0]
    if kind not in(1,2)or h[32:96]!=BOARD.encode().ljust(64,b'\0'):raise ValueError('Profilo o tipo non validi')
    try:release=h[96:160].split(b'\0',1)[0].decode('ascii')
    except UnicodeDecodeError as error:raise ValueError('Versione non valida')from error
    if h[96:160]!=encode_text(release,64):raise ValueError('Versione non canonica')
    for offset,value in[(160,sha(LAYOUT.encode())),(192,bytes.fromhex(PARTITIONS[1][3])),
        (224,bytes.fromhex(PARTITIONS[2][3])),(256,bytes.fromhex(PARTITIONS[4][3])),
        (352,bytes.fromhex(PARTITIONS[3][3])),(384,bytes.fromhex(PARTITIONS[4][3])),(480,sha(h[:480]))]:
        if h[offset:offset+32]!=value:raise ValueError('Profilo o integrità intestazione diversi')
    if not any(h[416:448])or not any(h[448:480]):raise ValueError('Origine mancante')
    squash_check(system,SYSTEM_SIZE);squash_check(www,WWW_SIZE)
    if h[288:320]!=sha(system)or h[320:352]!=sha(www):raise ValueError('Contenuto danneggiato')
    if kind==2 and(h[288:320]!=h[352:384]or h[320:352]!=h[384:416]):raise ValueError('Ripristino non originale')
    return dict(format=3,board=BOARD,release=release,kind='stock'if kind==2 else'modified',
        partitions=['mtd3 system','mtd4 www'],system_sha256=sha(system).hex(),www_sha256=sha(www).hex(),
        source_system_sha256=h[416:448].hex(),source_www_sha256=h[448:480].hex(),package_sha256=sha(package).hex(),
        root_kernel_boot_preserved=True,downgrade_allowed=True,integrity_verified=True,flash_written=False)

def verify_immutable(flash):
    if len(flash)!=0x800000:raise ValueError('Dimensione flash non valida')
    if sha(flash[:BOOT_CODE_SIZE]).hex()!=BOOT_CODE_SHA:raise ValueError('Bootloader diverso')
    for index in(1,2):
        name,start,size,expected=PARTITIONS[index]
        if sha(flash[start:start+size]).hex()!=expected:raise ValueError('Componente immutabile diverso: '+name)

def verify_base(flash,package,recovery=False):
    checked=check(package);verify_immutable(flash)
    if recovery:
        if checked['kind']!='stock':raise ValueError('Recupero consentito solo verso le applicazioni originali')
        return
    for index,offset in[(3,416),(4,448)]:
        name,start,size,_=PARTITIONS[index]
        if sha(flash[start:start+size])!=package[offset:offset+32]:raise ValueError('Origine diversa: '+name)

def simulate(flash,package,recovery=False):
    verify_base(flash,package,recovery)
    result=flash[:SYSTEM_OFFSET]+package[HEADER_SIZE:]+flash[WWW_OFFSET+WWW_SIZE:]
    assert result[:SYSTEM_OFFSET]==flash[:SYSTEM_OFFSET]and result[WWW_OFFSET+WWW_SIZE:]==flash[WWW_OFFSET+WWW_SIZE:]
    return result
