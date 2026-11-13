"""Exercise the real native gateway against a deterministic local PPPP peer."""
import json
import os
from pathlib import Path
import re
import socket
import struct
import subprocess
import tempfile
import threading
import time
import unittest
import urllib.error
import urllib.parse
import urllib.request

ROOT = Path(__file__).resolve().parents[1]
TABLE = list(map(int, re.search(r'\{(.*?)\}', (ROOT/'src/pppp_table.h').read_text(), re.S)[1].split(',')))
PASSWORD = 'local-test-only'

def codec(data, encode):
    h = [0, 0, 0, 0]
    for k in b'vstarcam2018':
        h = [(h[0]+k)&255, (h[1]-k)&255, (h[2]+k//3)&255, h[3]^k]
    previous = 0
    out = bytearray()
    for c in data:
        value = c ^ TABLE[(h[previous&3]+previous)&255]
        out.append(value)
        previous = value if encode else c
    return bytes(out)

class Peer(threading.Thread):
    def __init__(self):
        super().__init__(daemon=True)
        self.sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.sock.bind(('127.0.0.1',32108))
        self.sock.settimeout(.1)
        self.running = True
        self.sequences = {}
        self.calls = []
        self.mask = 0
        self.identity = "VE0000000TEST"
        self.uid = b'TEST\0\0\0\0'+struct.pack('>I',1)+b'LOCAL\0\0\0'
        self.light = 0
        self.drop_next_move = False
        self.delay_next_move = 0
        self.delay_next_stop = 0
        self.duplicate_fragments = False
        self.setup_error = False
        self.record_enabled = 1
        self.record_values = [-1]*21
        self.record_write_error = False
        self.motion_record = 0
        self.motion_record_values = [-1]*21
        self.motion_mode = 0
        self.motion_values = [-1]*21
        self.motion_armed = 0
        self.motion_sensitivity = 0
        self.motion_failure = ''
        self.ir_mode = 1
        self.ir_error = False
        self.foreign_discovery = False
        self.drop_discovery = 0
        self.drop_confirmation = 0
        self.drop_control = 0
        self.control_packets = []
        self.ack_without_reply = False
        self.foreign_close = False
        self.seen_requests = set()
        self.stop_waits_for_media_ack=False;self.stop_no_reply=False;self.pending_stop={};self.stop_confirmations=[]
        self.drop_next_status_reply=False;self.request_addresses=[]
        self.heartbeats=[];self.talk_frames=[];self.talk_ack=True;self.talk_ack_prefix=False;self.live_sessions=set();self.listen_address=None;self.data_acks=[];self.startup_media=b''
        self.start()

    def send(self, op, payload, address):
        self.sock.sendto(codec(bytes([241,op])+struct.pack('>H',len(payload))+payload,True),address)

    def run(self):
        while self.running:
            try:
                raw, address = self.sock.recvfrom(8192)
            except socket.timeout:
                continue
            data = codec(raw,False)
            op, body = data[1], data[4:]
            if op == 0x30:
                if self.drop_discovery:
                    self.drop_discovery -= 1
                    continue
                self.sequences[address] = 0
                self.seen_requests = {r for r in self.seen_requests if r[0] != address}
                if self.foreign_discovery:
                    self.send(0x41,b'TEST\0\0\0\0'+struct.pack('>I',99)+b'OTHER\0\0\0',address)
                self.send(0x41,self.uid,address)
            elif op == 0x41:
                if self.drop_confirmation:
                    self.drop_confirmation -= 1
                    continue
                self.send(0x42,body,address)
            elif op == 0xe0:
                self.heartbeats.append(address);self.send(0xe1,b'',address)
            elif op == 0xd1:
                self.data_acks.append(body)
                callback=getattr(self,'on_data_ack',None)
                if callback:callback(address,body)
                if address in self.pending_stop and len(body)>=6 and body[1]==1 and body[-2:]==b'\x70\x00':
                    self.pending_stop.pop(address);self.stop_confirmations.append(address)
                    self.live_sessions.discard(address)
                    self.control_reply(address,0x6037,b'var result=0;')
            elif op == 0xf0:
                self.live_sessions.discard(address)
                self.pending_stop.pop(address,None)
            elif op == 0xd0:
                if len(body)>=4 and body[1]==3:
                    self.talk_frames.append((struct.unpack('>H',body[2:4])[0],body[4:],time.monotonic()))
                    if self.talk_ack:self.send(0xd1,(b'\xd1\x03\x00\x02\xff\xff' if self.talk_ack_prefix else b'\xd1\x03\x00\x01')+body[2:4],address)
                    continue
                if self.foreign_close:
                    self.foreign_close = False
                    with socket.socket(socket.AF_INET,socket.SOCK_DGRAM) as foreign:
                        foreign.sendto(codec(b'\xf1\xf0\x00\x00',True),address)
                self.control_packets.append((address,body))
                if self.drop_control:
                    self.drop_control -= 1
                    continue
                # Include another sequence first, as in an aggregate SDK ACK.
                self.send(0xd1,b'\xd1\x00\x00\x02\xff\xff'+body[2:4],address)
                if self.ack_without_reply:continue
                request = (address,body[2:4])
                if request in self.seen_requests:
                    continue
                self.seen_requests.add(request)
                # Enforce the actual framing, including requests >255 bytes.
                if body[4:8]!=b'\x01\x0a\x00\x00' or int.from_bytes(body[8:11],'little')!=len(body)-12:
                    self.send(0xf0,b'',address)
                    continue
                path = body[16:].decode()
                parsed = urllib.parse.urlsplit(path)
                params = urllib.parse.parse_qs(parsed.query,keep_blank_values=True)
                action = parsed.path.removeprefix('/').removesuffix('.cgi')
                self.calls.append((action,params))
                self.request_addresses.append((action,address))
                if params.get('pwd') != [PASSWORD]:
                    answer = b'var result=-1;'
                elif action == 'get_status':
                    if self.drop_next_status_reply:
                        self.drop_next_status_reply=False;continue
                    answer = f'var sys_ver="mock";var realdeviceid="{self.identity}";var haveMotor=1;var ircut_forceon=1;var preset_value={self.mask};var watch_preset=0;'.encode()
                elif action == 'get_params':
                    if getattr(self,'motion_readback_drops_left',0):
                        self.motion_readback_drops_left-=1;continue
                    answer = f'var result=0;var alarm_motion_armed={self.motion_armed};var alarm_motion_sensitivity={self.motion_sensitivity};var wifi_pwd="fixture-private-value";'.encode()
                elif action == 'set_alarm':
                    fail=self.motion_failure=='alarm_once'
                    if fail:self.motion_failure=''
                    else:
                        self.motion_armed=int(params['motion_armed'][0])
                        self.motion_sensitivity=int(params['motion_sensitivity'][0])
                        if self.motion_armed:self.motion_readback_drops_left=getattr(self,'motion_readback_drops',0)
                    answer=f'var result={-1 if fail else 0};'.encode()
                elif action == 'livestream':
                    if params.get('streamid')==['16']:
                        if self.stop_no_reply:
                            self.live_sessions.discard(address);continue
                        if self.stop_waits_for_media_ack:
                            self.pending_stop[address]=True
                            self.send(0xd0,b'\xd1\x01\x70\x00discard pending media without parsing',address)
                            continue
                        self.stop_confirmations.append(address);self.live_sessions.discard(address)
                    else:
                        self.live_sessions.add(address)
                        if self.startup_media:self.send(0xd0,b'\xd1\x01\x00\x00'+self.startup_media,address)
                    answer=b'var result=0;'
                elif action == 'audiostream':
                    self.listen_address=address;answer=b'var result=0;'
                elif action == 'snapshot':
                    answer = b'\xff\xd8'+b'JPEG'*500+b'\xff\xd9' if params.get('sensor') == ['0'] else b''
                elif action == 'decoder_control':
                    command = int(params['command'][0])
                    if command in (0,2,4,6) and self.drop_next_move:
                        self.drop_next_move = False
                        continue
                    if command in (0,2,4,6) and self.delay_next_move:
                        delay=self.delay_next_move;self.delay_next_move=0;time.sleep(delay)
                    if command==1 and self.delay_next_stop:
                        delay=self.delay_next_stop;self.delay_next_stop=0;time.sleep(delay)
                    if 62 <= command <= 77:
                        self.mask &= ~(1<<(15-(command-62)))
                    elif 30 <= command <= 60 and command%2 == 0:
                        self.mask |= 1<<(15-(command-30)//2)
                    answer = f'var result=0;var command={command};'.encode()
                elif action == 'set_whiteled_value':
                    self.light = int(params['light'][0])
                    answer = b'var result=0;'
                elif action == 'get_whiteled_value':
                    answer = f'var light={self.light};var whiteled=100;'.encode()
                elif action == 'get_camera_params':
                    answer=f'var ircut={self.ir_mode};'.encode()
                elif action == 'camera_control':
                    if not self.ir_error and params.get('param')==['14'] and params.get('video')==['1']:
                        self.ir_mode=int(params['value'][0])
                    answer=f'var result={-1 if self.ir_error else 0};'.encode()
                elif action == 'trans_cmd_string':
                    if params.get('cmd')==['2017']:
                        command=int(params.get('command',['0'])[0])
                        kind=int(params.get('type',[str(command)])[0]);status=0
                        if command==3 and not self.record_write_error:
                            value=int(params['record_plan_enable'][0])
                            self.record_enabled=value-256 if value in (256,257) else value
                            self.record_values=[int(params[f'record_plan{i}'][0]) for i in range(1,22)]
                        elif command==1:
                            self.motion_record=int(params['motion_record_plan_enable'][0])
                            self.motion_record_values=[int(params[f'motion_record_plan{i}'][0]) for i in range(1,22)]
                        elif command==2:
                            if self.motion_failure=='detector_once':status=1;self.motion_failure=''
                            else:
                                self.motion_mode=int(params['motion_push_plan_enable'][0])
                                self.motion_values=[int(params[f'motion_push_plan{i}'][0]) for i in range(1,22)]
                        prefix={1:'motion_record',2:'motion_push',3:'record'}[kind]
                        enabled={1:self.motion_record,2:self.motion_mode,3:self.record_enabled}[kind]
                        values={1:self.motion_record_values,2:self.motion_values,3:self.record_values}[kind]
                        enable_key=prefix+('_plan_enable' if kind==3 else '_enable')
                        answer=(f'var result=0;var cmd=2017;var command={command};var type={kind};var status={status};var {enable_key}={enabled};'+''.join(f'var {prefix}_plan{i}={value};' for i,value in enumerate(values,1))).encode()
                    else:
                        answer = f'var result={-1 if self.setup_error else 0};var cmd=9000;var command=1;'.encode()
                elif action == 'get_record':
                    answer=b'var record_audio=1;var record_timer=5;var record_sd_status=1;var sdtotal=30000;var sdfree=28000;var record_time_enable=0;'
                else:
                    answer = b'var result=0;'
                if action=='trans_cmd_string' and params.get('command')==['3'] and getattr(self,'drop_record_reply_once',False):
                    self.drop_record_reply_once=False
                    continue
                code = {'livestream':0x6037,'audiostream':0x6031,'get_status':0x6001,'get_params':0x6002,'set_alarm':0x600c,'get_record':0x6006,'get_camera_params':0x6003,'snapshot':0x6015,'decoder_control':0x6019,'set_wifi':0x6011,'camera_control':0x6012,'get_whiteled_value':0x7116,'set_whiteled_value':0x7117,'set_sensor_preset':0x60b7,'trans_cmd_string':0x60d1}[action]
                self.control_reply(address,code,answer)

    def control_reply(self,address,code,answer):
        message=b'\x01\x0a'+struct.pack('<H',code)+len(answer).to_bytes(3,'little')+b'\x01'+answer
        for i in range(0,len(message),600):
            sequence=self.sequences[address]
            payload=b'\xd1\x00'+struct.pack('>H',sequence)+message[i:i+600]
            self.send(0xd0,payload,address)
            if self.duplicate_fragments:self.send(0xd0,payload,address)
            self.sequences[address]+=1

    def notify(self,address,body,code=0x7116):
        sequence=self.sequences[address]
        self.send(0xd0,b'\xd1\x00'+struct.pack('>H',sequence)+body,address)
        self.sequences[address]+=1

    def close(self):
        self.running = False
        self.join(1)
        self.sock.close()

class Gateway(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.build = tempfile.TemporaryDirectory()
        supplied=os.environ.get('QC5_GATEWAY_TEST_COMMAND')
        if supplied:
            cls.command=json.loads(supplied)
            if not isinstance(cls.command,list) or not cls.command or any(not isinstance(s,str) or not s for s in cls.command):
                raise ValueError('QC5_GATEWAY_TEST_COMMAND must be a nonempty JSON argv list')
        else:
            binary=str(Path(cls.build.name)/'gateway')
            subprocess.run(['cc','-DQC5_HOST_TEST','-O2','-Wall','-Wextra','-Werror',str(ROOT/'src/qc5_gateway.c'),str(ROOT/'src/qc5_mp4.c'),str(ROOT/'src/qc5_wifi_store.c'),'-o',binary],check=True)
            cls.command=[binary]

    @classmethod
    def tearDownClass(cls):
        cls.build.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.folder = Path(self.tmp.name)
        self.config = self.folder/'config.txt'
        self.config.write_text('CAM_NAME="Camera test"\nSSID=""\nPWD=""\nRECORD_CONTINUOUS=1\n')
        nvs = bytearray(0x40000)
        nvs[:12] = b'PCAM'+struct.pack('<II',0x20000,0)
        payload = bytearray(18848)
        payload[0x138:0x138+len(PASSWORD)] = PASSWORD.encode()
        record = b'app_param\0'+payload
        struct.pack_into('<IIII',nvs,12,0x900,0x20900,len(record),0)
        nvs[0x900:0x900+len(record)] = record
        (self.folder/'nvs').write_bytes(nvs)
        (self.folder/'index.html').write_text('QC5 LOCAL PAGE')
        (self.folder/'live.html').write_text('QC5 LOCAL PREVIEW')
        self.peer = Peer()
        self.addCleanup(self.peer.close)
        self.addCleanup(self.tmp.cleanup)
        sock = socket.socket()
        sock.bind(('127.0.0.1',0))
        self.port = sock.getsockname()[1]
        sock.close()
        self.extra_args = []
        self.sd_mounted = True
        self.station = self.folder/'station-state'
        self.station.write_text('0')
        self.default_memory_file = self.folder/'meminfo'
        self.default_memory_file.write_text('MemAvailable: 8192 kB\n')
        self.start_gateway()
        self.addCleanup(self.stop_gateway)

    def start_gateway(self):
        self.process = subprocess.Popen(self.command+['--nvs',str(self.folder/'nvs'),
            '--config',str(self.config),'--sd',str(self.folder),'--webroot',str(self.folder),'--run-dir',str(self.folder),
            '--bind','127.0.0.1','--port',str(self.port)]+self.extra_args,stderr=subprocess.DEVNULL,
            env=dict(os.environ,QC5_TEST_STATION_STATE=str(self.station),QC5_TEST_STATION_SSID='New network',QC5_TEST_AP_MODE=getattr(self,'ap_mode','0'),QC5_TEST_DISCOVERY_IP='127.0.0.1',
                     QC5_TEST_SD_ROOT=str(self.folder) if self.sd_mounted else '',
                     QC5_TEST_NATIVE_APP=str(self.native_app) if hasattr(self,'native_app') else '',
                     QC5_TEST_MOTION_COUNTER=str(self.motion_counter_file) if hasattr(self,'motion_counter_file') else '',
                     QC5_TEST_RECORD_POLICY=getattr(self,'record_policy_autostart','0'),
                     QC5_TEST_RECORD_ACTIVE=getattr(self,'record_active','0'),
                     QC5_TEST_RECORD_ACTIVE_FILE=str(self.record_active_file) if hasattr(self,'record_active_file') else '',
                     QC5_TEST_MEMINFO=str(self.memory_file) if hasattr(self,'memory_file') else os.environ.get('QC5_TEST_MEMINFO',str(self.default_memory_file))))
        for i in range(500):
            if self.process.poll() is not None:
                self.fail("Gateway exited at startup: " + str(self.process.returncode))
            try:
                with socket.create_connection(('127.0.0.1',self.port),timeout=.1):
                    break
            except OSError:
                time.sleep(.01)
        else:
            self.fail('HTTP listener failed')

    def stop_gateway(self):
        if self.process.poll() is None:
            self.process.terminate()
        self.process.wait(8)

    def get(self,path):
        return urllib.request.urlopen(f'http://127.0.0.1:{self.port}'+path,timeout=8)

    def api(self,command,**params):
        with self.get('/cgi-bin/webui?'+urllib.parse.urlencode(dict(command=command,**params))) as r:
            return json.load(r)

    def test_automatic_camera_id_comes_from_current_unit(self):
        self.config.write_text('CAM_ID="auto"\nSSID=""\nPWD=""\n')
        self.peer.identity = 'VE0000001TEST'
        self.api('wifi_apply')  # reload the SD configuration
        self.assertEqual(self.api('sysinfo')['CAM_ID'], 'qc5-ve0000001test')

    def test_shared_sd_urls_use_the_current_units_identity(self):
        self.config.write_text('CAM_ID="auto"\n'
            'RESTREAMER_URL="http://nas/player.html?src={CAM_ID}"\n'
            'RTSP_URL="rtsp://nas:8554/{CAM_ID}"\n')
        self.peer.identity = 'VE0000002TEST'
        self.api('wifi_apply')
        data = self.api('sysinfo')
        self.assertEqual(data['CAM_ID'], 'qc5-ve0000002test')
        self.assertEqual(data['RESTREAMER_URL'], 'http://nas/player.html?src=qc5-ve0000002test')
        self.assertEqual(data['RTSP_URL'], '') # Advertise only an active camera listener.

    def test_missing_identity_cannot_fall_back_to_another_cameras_stream(self):
        self.config.write_text('CAM_ID="auto"\nRESTREAMER_URL="http://nas/player.html?src={CAM_ID}"\n')
        self.peer.identity = ''
        self.api('wifi_apply')
        data = self.api('sysinfo')
        self.assertEqual(data['CAM_ID'], '')
        self.assertEqual(data['RESTREAMER_URL'], '')

    def test_native_authentication_and_fragmented_jpeg(self):
        self.assertTrue(self.api('sysinfo')['adapter']['bridge'])
        self.peer.duplicate_fragments = True
        with self.get('/cgi-bin/snapshot') as r:
            self.assertEqual(r.headers['Content-Type'],'image/jpeg')
            self.assertEqual(r.read(),b'\xff\xd8'+b'JPEG'*500+b'\xff\xd9')
        self.assertTrue(self.api('sysinfo')['adapter']['bridge'])

    def test_close_from_another_udp_session_does_not_interrupt_snapshot(self):
        self.peer.foreign_close=True
        with self.get('/cgi-bin/snapshot') as response:
            self.assertEqual(response.status,200)
            self.assertTrue(response.read().endswith(b'\xff\xd9'))

    def test_incomplete_http_headers_do_not_move_motors(self):
        with socket.create_connection(('127.0.0.1',self.port),timeout=3) as client:
            client.sendall(b'GET /cgi-bin/webui?command=ptzu HTTP/1.1\r\nHost: local\r\n')
            client.shutdown(socket.SHUT_WR)
            response=client.recv(4096)
        self.assertTrue(response.startswith(b'HTTP/1.1 400 '),response)
        self.assertFalse(any(action=='decoder_control' for action,_ in self.peer.calls))

    def test_missing_sd_does_not_allocate_a_native_preset(self):
        self.stop_gateway();self.sd_mounted=False;self.start_gateway()
        self.assertFalse(self.api('preset_save',preset_name='Home')['ok'])
        self.assertEqual(self.peer.mask,0)
        self.assertFalse((self.folder/'qc5').exists())

    def test_preset_directory_symlink_is_rejected_before_native_save(self):
        outside=self.folder/'outside';outside.mkdir()
        (self.folder/'qc5').symlink_to(outside,target_is_directory=True)
        self.assertFalse(self.api('preset_save',preset_name='Home')['ok'])
        self.assertEqual(self.peer.mask,0)
        self.assertEqual(list(outside.iterdir()),[])

    def test_empty_preset_slot_is_not_interpreted_as_slot_zero(self):
        self.assertTrue(self.api('preset_save',preset_name='Home')['ok'])
        path=self.folder/'qc5/qc5-ve0000000test/presets/Home'
        path.write_text('QC5_SLOT1\n\n')
        before=len(self.peer.calls)
        self.assertFalse(self.api('preset_go',preset_name='Home')['ok'])
        self.assertFalse(any(action=='decoder_control' for action,_ in self.peer.calls[before:]))

    def test_bad_sd_config_reload_preserves_the_working_configuration(self):
        self.assertEqual(self.api('sysinfo')['CAM_NAME'],'Camera test')
        for broken in ('CAM_NAME','PWD','UNUSED_OPTION'):
            self.config.write_text(broken+'="incomplete\n')
            self.assertFalse(self.api('wifi_apply')['ok'])
            self.assertEqual(self.api('sysinfo')['CAM_NAME'],'Camera test')

    def test_config_io_error_is_refused_at_startup(self):
        self.stop_gateway();self.config.unlink();self.config.mkdir()
        r=subprocess.run(self.command+['--nvs',str(self.folder/'nvs'),'--config',str(self.config),
                          '--bind','127.0.0.1','--port',str(self.port)],capture_output=True,timeout=3)
        self.assertEqual(r.returncode,4)

    def test_day_night_reports_the_confirmed_native_mode(self):
        for command, value in [('ircutday',0),('ircutnig',2),('irauto',1)]:
            self.assertTrue(self.api(command)['ok'])
            self.assertEqual(self.peer.ir_mode,value)
        self.peer.ir_error = True
        self.assertFalse(self.api('ircutday')['ok'])
        self.assertEqual(self.peer.ir_mode,1)

    def completion_calls(self):
        return [params for action,params in self.peer.calls if action=='trans_cmd_string']

    def start_completion(self, enabled=True, connected=True):
        self.stop_gateway()
        self.config.write_text(f'LOCAL_SETUP={1 if enabled else 0}\n')
        self.station.write_text('1' if connected else '0')
        self.extra_args = ['--complete-local-setup']
        self.start_gateway()

    def wait_completion(self, state):
        until = time.monotonic()+5
        while time.monotonic()<until:
            actual = self.api('sysinfo')['adapter']['local_setup']
            if actual==state:
                return
            time.sleep(.05)
        self.fail(f'Expected setup state {state}, got {actual}')

    def test_local_completion_waits_for_station_connection_then_runs_once(self):
        self.start_completion(connected=False)
        self.wait_completion('attesa_wifi')
        self.assertFalse(self.completion_calls())
        self.station.write_text('1')
        self.wait_completion('completata_locale')
        self.assertEqual(len(self.completion_calls()),1)
        call = self.completion_calls()[0]
        self.assertEqual((call['cmd'],call['command'],call['userid']),(['9000'],['1'],['local']))
        time.sleep(3.1)
        self.assertEqual(len(self.completion_calls()),1)

    def test_local_completion_preserves_owner_in_other_nvs_bank(self):
        nvs = bytearray((self.folder/'nvs').read_bytes())
        length = struct.unpack_from('<I',nvs,20)[0]
        nvs[0x20000:0x2000c] = nvs[:12]
        struct.pack_into('<IIII',nvs,0x2000c,0x20900,0x900,length,0)
        nvs[0x20900:0x20900+length] = nvs[0x900:0x900+length]
        start = 0x20900+len(b'app_param\0')+0x1e0
        nvs[start:start+15] = b'existing-owner\0'
        (self.folder/'nvs').write_bytes(nvs)
        self.start_completion()
        self.wait_completion('configurazione_preservata')
        self.assertFalse(self.completion_calls())
        self.assertEqual((self.folder/'nvs').read_bytes(),bytes(nvs))

    def test_local_completion_can_be_disabled(self):
        self.start_completion(enabled=False)
        self.wait_completion('disabilitato')
        self.assertFalse(self.completion_calls())

    def test_saved_local_owner_is_reaffirmed_after_boot(self):
        nvs=bytearray((self.folder/'nvs').read_bytes())
        start=0x900+len(b'app_param\0')+0x1e0
        nvs[start:start+6]=b'local\0'
        (self.folder/'nvs').write_bytes(nvs)
        self.start_completion()
        self.wait_completion('completata_locale')
        self.assertEqual(len(self.completion_calls()),1)
        self.assertEqual((self.folder/'nvs').read_bytes(),bytes(nvs))

    def test_rejected_setup_command_is_not_repeated_automatically(self):
        self.peer.setup_error = True
        self.start_completion()
        self.wait_completion('esito_da_verificare')
        time.sleep(3.1)
        self.assertEqual(len(self.completion_calls()),1)

    def test_local_media_urls_are_read_from_sd_and_json_escaped(self):
        self.config.write_text('CAM_NAME="Camera test"\nCAM_ID=veepai-test\n'
            'RESTREAMER_URL="http://nas/stream.html?src=veepai&mode=mse"\n'
            'REC_API="http://nas:8586"\nRTSP_URL="rtsp://nas:8554/veepai"\n')
        # Production reads this configuration when our gateway starts.
        self.api('wifi_apply')
        data = self.api('sysinfo')
        self.assertEqual(data['CAM_ID'], 'veepai-test')
        self.assertEqual(data['REC_API'], 'http://nas:8586')
        self.assertEqual(data['RESTREAMER_URL'], 'http://nas/stream.html?src=veepai&mode=mse')
        self.assertEqual(data['RTSP_URL'], '')

    def test_reassigned_ip_does_not_authenticate_another_uid(self):
        self.assertTrue(self.api('sysinfo')['adapter']['bridge'])
        address=next(iter(self.peer.sequences))
        self.peer.uid=b'TEST\0\0\0\0'+struct.pack('>I',99)+b'OTHER\0\0\0'
        self.peer.send(0xf0,b'',address)
        time.sleep(.1)
        before=len(self.peer.calls)
        self.assertFalse(self.api('ptzl')['ok'])
        self.assertEqual(self.peer.calls[before:],[])

    def test_explicit_uid_is_checked_before_sending_credentials(self):
        self.stop_gateway()
        self.extra_args=['--expected-uid','TEST-99-OTHER']
        self.start_gateway()
        self.assertFalse(self.api('sysinfo')['adapter']['bridge'])
        self.assertEqual(self.peer.calls,[])

    def test_automatic_discovery_selects_expected_unit_among_other_replies(self):
        self.stop_gateway()
        self.peer.foreign_discovery=True
        self.extra_args=['--host','auto','--expected-uid','TEST-1-LOCAL']
        self.start_gateway()
        self.assertTrue(self.api('sysinfo')['adapter']['bridge'])

    def test_automatic_discovery_without_identity_cannot_select_arbitrary_unit(self):
        self.stop_gateway()
        self.extra_args=['--host','auto']
        self.start_gateway()
        self.assertFalse(self.api('sysinfo')['adapter']['bridge'])
        self.assertFalse(self.peer.calls)

    def test_lost_local_discovery_is_retried_within_the_same_deadline(self):
        self.stop_gateway();self.peer.drop_discovery=2;self.start_gateway()
        self.assertTrue(self.api('sysinfo')['adapter']['bridge'])
        self.assertEqual(self.peer.drop_discovery,0)

    def test_lost_handshake_confirmation_is_retried_before_credentials(self):
        self.stop_gateway();self.peer.drop_confirmation=2;self.start_gateway()
        self.assertTrue(self.api('sysinfo')['adapter']['bridge'])
        self.assertEqual(self.peer.drop_confirmation,0)

    def test_automatic_discovery_retries_without_switching_identity(self):
        self.stop_gateway();self.peer.drop_discovery=1;self.peer.drop_confirmation=1
        self.peer.foreign_discovery=True
        self.extra_args=['--host','auto','--expected-uid','TEST-1-LOCAL'];self.start_gateway()
        self.assertTrue(self.api('sysinfo')['adapter']['bridge'])
        self.assertEqual((self.peer.drop_discovery,self.peer.drop_confirmation),(0,0))

    def test_lost_control_request_retries_the_same_sequence_and_payload(self):
        self.peer.drop_control=2
        self.assertTrue(self.api('sysinfo')['adapter']['bridge'])
        packets=self.peer.control_packets
        self.assertGreaterEqual(len(packets),3)
        self.assertEqual(packets[0],packets[1]);self.assertEqual(packets[1],packets[2])
        self.assertEqual(sum(name=='get_status' for name,_ in self.peer.calls),1)

    def test_acknowledged_control_is_not_reissued_when_reply_is_missing(self):
        self.peer.ack_without_reply=True
        self.assertFalse(self.api('sysinfo')['adapter']['bridge'])
        self.assertEqual(len(self.peer.control_packets),1)

    def test_split_notification_arriving_while_idle(self):
        self.assertTrue(self.api('sysinfo')['adapter']['bridge'])
        address=next(iter(self.peer.sequences))
        body=b'var light=0;'+b' '*800
        frame=b'\x01\x0a\x16\x71'+len(body).to_bytes(3,'little')+b'\x01'+body
        self.peer.notify(address,frame[:100])
        time.sleep(.1)
        self.peer.notify(address,frame[100:])
        time.sleep(.1)
        with self.get('/cgi-bin/snapshot') as r:
            self.assertEqual(r.read()[:2],b'\xff\xd8')

    def test_diagonal_uses_bounded_moves_and_stops(self):
        self.assertTrue(self.api('ptzlu')['ok'])
        commands=[p['command'][0] for a,p in self.peer.calls if a=='decoder_control']
        self.assertEqual(commands,['4','1','0','1'])

    def test_preset_reversed_native_mask_recall_delete_and_restart(self):
        self.peer.mask = 0x8000
        self.assertTrue(self.api('preset_save',preset_name='Home')['ok'])
        self.assertEqual((self.folder/'qc5/qc5-ve0000000test/presets/Home').read_text(),'QC5_SLOT1\n1\n')
        self.assertEqual(self.peer.mask,0xc000)
        self.assertTrue(self.api('preset_go',preset_name='Home')['ok'])
        self.assertIn(('decoder_control',{'command':['33'],'channel':['0'],'loginuse':['admin'],'loginpas':[PASSWORD],'user':['admin'],'pwd':[PASSWORD]}),self.peer.calls)
        self.assertTrue(self.api('preset_del',preset_name='Home')['ok'])
        self.assertEqual(self.peer.mask,0x8000)
        self.assertFalse((self.folder/'qc5/qc5-ve0000000test/presets/Home').exists())
        self.assertEqual(self.api('preset_list')['presets'],[])

    def test_move_timeout_still_sends_stop(self):
        self.peer.drop_next_move = True
        self.assertFalse(self.api('ptzl')['ok'])
        commands = [p['command'][0] for a,p in self.peer.calls if a=='decoder_control']
        self.assertEqual(commands,['4','1'])

    def test_moving_sd_to_another_unit_does_not_reuse_its_presets(self):
        self.assertTrue(self.api('preset_save',preset_name='Home')['ok'])
        self.config.write_text('CAM_ID="custom-alias"\n')
        self.api('wifi_apply')
        self.assertEqual(self.api('preset_list')['presets'], ['Home'])
        self.stop_gateway()
        self.peer.identity = 'VE0000099OTHER'
        self.peer.mask = 0
        self.start_gateway()
        self.assertEqual(self.api('preset_list')['presets'], [])
        before = len(self.peer.calls)
        self.assertFalse(self.api('preset_go',preset_name='Home')['ok'])
        self.assertFalse(any(a=='decoder_control' for a, _ in self.peer.calls[before:]))
        self.assertTrue((self.folder/'qc5/qc5-ve0000000test/presets/Home').is_file())

    def test_delayed_move_ack_is_accepted_only_with_confirmed_stop(self):
        self.peer.delay_next_move=.35
        started=time.monotonic()
        self.assertTrue(self.api('ptzl')['ok'])
        self.assertLess(time.monotonic()-started,1.5)
        commands=[p['command'][0] for a,p in self.peer.calls if a=='decoder_control']
        self.assertEqual(commands,['4','1'])

    def test_slow_stop_confirmation_does_not_report_false_failure(self):
        self.peer.delay_next_stop=1.8
        self.assertTrue(self.api('ptzl')['ok'])
        commands=[p['command'][0] for a,p in self.peer.calls if a=='decoder_control']
        self.assertEqual(commands,['4','1'])

    def test_wifi_literal_encoding_and_validation(self):
        self.config.write_text('SSID="Casa #=&"\nPWD="ab$()#&=cd"\nWIFI_APPLY=1\n')
        self.assertTrue(self.api('wifi_apply')['ok'])
        params = [p for a,p in self.peer.calls if a=='set_wifi'][0]
        self.assertEqual(params['ssid'],['Casa #=&'])
        self.assertEqual(params['wpa_psk'],['ab$()#&=cd'])
        self.config.write_text('SSID=Casa\nPWD=corta\n')
        self.assertFalse(self.api('wifi_apply')['ok'])
        self.assertEqual(len([1 for a,p in self.peer.calls if a=='set_wifi']),1)

    def test_saved_credentials_do_not_suppress_retry_when_station_is_disconnected(self):
        data=bytearray((self.folder/'nvs').read_bytes());payload=b'Casa\0'.ljust(64,b'\0')+b'password123\0'.ljust(64,b'\0')
        record=b'VEEPAI-WIFI-STA\0'+payload;offset=0x6000
        struct.pack_into('<IIII',data,28,offset,0x20000+offset,len(record),0);data[offset:offset+len(record)]=record
        (self.folder/'nvs').write_bytes(data);self.config.write_text('SSID=Casa\nPWD=password123\nWIFI_APPLY=1\n')
        self.station.write_text('1');self.assertTrue(self.api('wifi_apply')['ok']);self.assertFalse(any(a=='set_wifi' for a,_ in self.peer.calls))
        self.station.write_text('0');self.assertTrue(self.api('wifi_apply')['ok']);self.assertEqual(len([a for a,_ in self.peer.calls if a=='set_wifi']),1)

    def test_http_does_not_expose_passwords_or_private_files(self):
        for path in ['/config.txt','/../nvs','/qc5/presets/Home','/src/qc5_gateway.c']:
            with self.assertRaises(urllib.error.HTTPError) as error:
                self.get(path)
            self.assertEqual(error.exception.code,404)
        self.assertNotIn(PASSWORD,json.dumps(self.api('sysinfo')))
        self.assertFalse(self.api('preset_save',preset_name='../config.txt')['ok'])
        self.assertFalse(self.api('reboot')['ok'])

    def test_preview_cache_limits_native_captures_and_pauses_for_ir(self):
        for i in range(4):
            with self.get('/cgi-bin/snapshot') as r:
                self.assertEqual(r.read()[:2],b'\xff\xd8')
        self.assertEqual(len([1 for a,p in self.peer.calls if a=='snapshot']),1)
        self.assertTrue(self.api('ircutnig')['ok'])
        with self.get('/cgi-bin/snapshot') as r:
            self.assertEqual(r.read()[:2],b'\xff\xd8')
        self.assertEqual(len([1 for a,p in self.peer.calls if a=='snapshot']),1)

    def test_white_light_readback(self):
        self.assertTrue(self.api('wlon')['ok'])
        self.assertEqual(self.peer.light,1)
        self.assertTrue(self.api('wlof')['ok'])
        self.assertEqual(self.peer.light,0)

    def provision_start(self,ready=True):
        self.stop_gateway();self.extra_args=['--provision']
        if ready:(self.folder/'wifi-controller').write_text('QC5_WIFI_CONTROLLER_1\n')
        self.start_gateway()
        with self.get('/setup/status') as r:return json.load(r)

    def provision_post(self,**changes):
        with self.get('/setup/status') as r:state=json.load(r)
        fields=dict(ssid='New network',pwd='new-password',open='0',token=state['token']);fields.update(changes)
        request=urllib.request.Request(f'http://127.0.0.1:{self.port}/setup/wifi',data=urllib.parse.urlencode(fields).encode(),headers={'Content-Type':'application/x-www-form-urlencoded'})
        with urllib.request.urlopen(request,timeout=8) as r:return json.load(r)

    def provision_nvs(self,ssid='New network',password='new-password'):
        data=bytearray((self.folder/'nvs').read_bytes())
        record=b'VEEPAI-WIFI-STA\0'+ssid.encode().ljust(64,b'\0')+password.encode().ljust(64,b'\0')
        struct.pack_into('<IIII',data,28,0x6000,0x26000,len(record),0);data[0x6000:0x6000+len(record)]=record
        (self.folder/'nvs').write_bytes(data)
        self.provision_tag(ssid,password)

    def provision_tag(self,ssid,password):
        tag=14695981039346656037
        for b in ssid.encode()+b'\0'+password.encode()+bytes([not bool(password)]):tag=((tag^b)*1099511628211)&((1<<64)-1)
        (self.folder/'wifi-committed').write_text(f'{tag:016x}\n')

    def provision_profile(self,ssid='New network',password='new-password'):
        (self.folder/'wifi-profile').write_bytes(ssid.encode().ljust(64,b'\0')+password.encode().ljust(64,b'\0'))
        self.provision_tag(ssid,password)

    def wait_wifi_saved(self):
        until=time.monotonic()+5
        while 'PWD="new-password"' not in self.config.read_text() and time.monotonic()<until:time.sleep(.05)

    def test_active_portal_publishes_private_monotonic_activity(self):
        self.ap_mode='1';before=self.config.read_bytes();self.provision_start()
        path=self.folder/'wifi-portal-active'
        self.assertTrue(path.is_file());self.assertEqual(path.stat().st_mode&0o777,0o600)
        self.assertLess(abs(int(path.read_text())-int(time.clock_gettime(time.CLOCK_MONOTONIC)*1000)),6000)
        self.assertEqual(self.config.read_bytes(),before)

    def test_station_requests_do_not_hold_hotspot_recovery(self):
        self.provision_start();self.assertFalse((self.folder/'wifi-portal-active').exists())

    def test_portal_activity_refuses_temporary_symlink(self):
        self.ap_mode='1';before=self.config.read_bytes()
        (self.folder/'.wifi-portal-active-new').symlink_to(self.config)
        self.provision_start();self.assertEqual(self.config.read_bytes(),before)
        self.assertFalse((self.folder/'wifi-portal-active').exists())

    def test_portal_associated_profile_is_saved_to_sd_before_native_commit(self):
        self.provision_start();old=self.config.read_bytes();self.provision_profile();self.station.write_text('1');self.wait_wifi_saved()
        self.assertIn('PWD="new-password"',self.config.read_text());self.assertEqual((self.folder/'.qc5-config.previous').read_bytes(),old)
        with self.get('/setup/status') as r:self.assertEqual(json.load(r)['state'],'wifi_salvato_sd_native_in_attesa')
        self.assertFalse(any(a=='set_wifi' for a,_ in self.peer.calls))

    def test_portal_delayed_native_commit_updates_state_without_rewriting_sd(self):
        self.provision_start();self.provision_profile();self.station.write_text('1');self.wait_wifi_saved()
        before=self.config.stat().st_mtime_ns;self.provision_nvs()
        until=time.monotonic()+5;state=''
        while time.monotonic()<until:
            with self.get('/setup/status') as r:state=json.load(r)['state']
            if state=='wifi_salvato_camera_e_sd':break
            time.sleep(.1)
        self.assertEqual(state,'wifi_salvato_camera_e_sd');self.assertEqual(self.config.stat().st_mtime_ns,before)

    def test_portal_association_without_link_does_not_save_sd(self):
        self.provision_start();old=self.config.read_bytes();self.provision_profile();self.station.write_text('0')
        time.sleep(3.3);self.assertEqual(self.config.read_bytes(),old)

    def test_portal_association_must_match_marker_and_current_ssid(self):
        self.provision_start();old=self.config.read_bytes();self.provision_profile(ssid='Different network');self.station.write_text('1')
        time.sleep(3.3);self.assertEqual(self.config.read_bytes(),old)
        self.provision_profile();self.provision_tag('Different network','new-password')
        time.sleep(3.3);self.assertEqual(self.config.read_bytes(),old)

    def test_portal_association_file_is_private_and_symlink_is_rejected(self):
        self.provision_start();old=self.config.read_bytes();self.provision_profile();self.station.write_text('1')
        p=self.folder/'wifi-profile';data=p.read_bytes();p.unlink();other=self.folder/'private-profile';other.write_bytes(data);p.symlink_to(other)
        time.sleep(3.3);self.assertEqual(self.config.read_bytes(),old)
        with self.assertRaises(urllib.error.HTTPError) as e:self.get('/wifi-profile')
        self.assertEqual(e.exception.code,404)

    @unittest.skipUnless(os.environ.get('QC5_WIFI_BOOT_COMMAND'),'native supervisor test binary required')
    def test_qr_native_commit_failure_survives_next_boot_using_sd(self):
        self.config.write_text('HACK_ENABLE=1\nBOOT_MODE=full\nSSID=""\nPWD=""\n')
        self.provision_start();self.station.write_text('1')
        command=json.loads(os.environ['QC5_WIFI_BOOT_COMMAND'])
        env=dict(os.environ,QC5_CONFIG=str(self.config),QC5_RUN=str(self.folder))
        first=subprocess.run(command+['qr_readback_failure'],env=env,capture_output=True,text=True,timeout=5)
        self.assertEqual(first.returncode,0,first.stderr);self.assertEqual(json.loads(first.stdout)['saved'],'')
        until=time.monotonic()+5
        while 'PWD="test-password"' not in self.config.read_text() and time.monotonic()<until:time.sleep(.05)
        self.assertIn('SSID="New network"',self.config.read_text());self.assertIn('PWD="test-password"',self.config.read_text())
        with self.get('/setup/status') as r:self.assertEqual(json.load(r)['state'],'wifi_salvato_sd_native_in_attesa')
        # Simulate power loss: a fresh supervisor process has no previous native
        # RAM/NVS profile, but uses the same durable SD configuration.
        run=self.folder/'after-power-cycle';run.mkdir();env['QC5_RUN']=str(run)
        second=subprocess.run(command+['manual'],env=env,capture_output=True,text=True,timeout=5)
        self.assertEqual(second.returncode,0,second.stderr);data=json.loads(second.stdout)
        self.assertEqual((data['joins'],data['aps'],data['saved']),(1,0,'New network'))

    def test_portal_disabled_without_explicit_boot_mode(self):
        with self.get('/setup/status') as r:self.assertFalse(json.load(r)['enabled'])
        with self.assertRaises(urllib.error.HTTPError) as e:self.provision_post()
        self.assertEqual(e.exception.code,503);self.assertFalse(any(a=='set_wifi' for a,_ in self.peer.calls))

    def test_portal_requires_cold_native_supervisor(self):
        state=self.provision_start(ready=False);self.assertFalse(state['enabled'])
        with self.assertRaises(urllib.error.HTTPError) as e:self.provision_post()
        self.assertEqual(e.exception.code,503)

    def test_portal_password_never_enters_sd_before_connection(self):
        self.provision_start();old=self.config.read_bytes();self.assertTrue(self.provision_post()['ok'])
        time.sleep(1.3);self.assertEqual(self.config.read_bytes(),old)
        self.assertIn('PWD="new-password"',(self.folder/'wifi-request').read_text());self.assertFalse(any(a=='set_wifi' for a,_ in self.peer.calls))
        with self.get('/setup/status') as r:self.assertNotIn('new-password',r.read().decode())

    def test_portal_rejects_wrong_token_without_changing_network(self):
        self.provision_start()
        with self.assertRaises(urllib.error.HTTPError) as e:self.provision_post(token='0'*32)
        self.assertEqual(e.exception.code,403);self.assertFalse(any(a=='set_wifi' for a,_ in self.peer.calls))

    def test_portal_safe_mode_blocks_network_changes(self):
        self.provision_start();(self.folder/'QC5-SAFE-MODE').touch()
        with self.assertRaises(urllib.error.HTTPError) as e:self.provision_post()
        self.assertEqual(e.exception.code,503);self.assertFalse(any(a=='set_wifi' for a,_ in self.peer.calls))

    def test_portal_rejects_short_password(self):
        self.provision_start();self.assertFalse(self.provision_post(pwd='short')['ok'])
        self.assertFalse(any(a=='set_wifi' for a,_ in self.peer.calls))

    def test_portal_requires_explicit_open_network(self):
        self.provision_start();self.assertFalse(self.provision_post(pwd='')['ok'])
        self.assertTrue(self.provision_post(pwd='',open='1')['ok'])

    def test_portal_verified_native_commit_is_copied_to_sd(self):
        self.provision_start();old=self.config.read_bytes();self.provision_nvs();self.station.write_text('1')
        until=time.monotonic()+5
        while self.config.read_bytes()==old and time.monotonic()<until:time.sleep(.1)
        self.assertIn('PWD="new-password"',self.config.read_text())
        self.assertEqual((self.folder/'.qc5-config.previous').read_bytes(),old)
        self.assertFalse(any(a=='set_wifi' for a,_ in self.peer.calls),'QR/native commit must not restart association')

    def test_portal_stale_marker_is_not_saved(self):
        self.provision_start();old=self.config.read_bytes();self.provision_nvs(ssid='Different network');self.station.write_text('1')
        time.sleep(3.3);self.assertEqual(self.config.read_bytes(),old)

    def test_portal_unconfirmed_credentials_are_not_saved(self):
        self.provision_start();old=self.config.read_bytes();self.provision_nvs();self.station.write_text('0')
        time.sleep(3.3);self.assertEqual(self.config.read_bytes(),old)

    def test_ap_root_serves_setup_page(self):
        self.ap_mode='1';(self.folder/'setup.html').write_text('LOCAL WIFI SETUP');self.provision_start()
        with self.get('/') as r:self.assertEqual(r.read(),b'LOCAL WIFI SETUP')

    def test_portal_rejects_duplicate_and_chunked_http_fields(self):
        self.provision_start()
        with self.get('/setup/status') as r:token=json.load(r)['token']
        body=urllib.parse.urlencode(dict(ssid='New network',pwd='new-password',open='0',token=token)).encode()
        for extra,tail in [(b'Content-Length: '+str(len(body)).encode()+b'\r\n',b''),(b'Transfer-Encoding: chunked\r\n',b''),(b'',b'&ssid=Other')]:
            payload=body+tail
            headers=b'POST /setup/wifi HTTP/1.1\r\nHost: localhost\r\nContent-Type: application/x-www-form-urlencoded\r\nContent-Length: '+str(len(payload)).encode()+b'\r\n'+extra+b'\r\n'
            with socket.create_connection(('127.0.0.1',self.port),timeout=3) as connection:
                connection.sendall(headers+payload);answer=connection.recv(4096)
                self.assertNotIn(b'"ok":true',answer)
        self.assertFalse(any(a=='set_wifi' for a,_ in self.peer.calls))

    def record_file(self,name='120000_100.mp4',day='2026-10-26',identity=None,complete=True):
        root=self.folder/(identity or self.peer.identity)/'video'/day
        root.mkdir(parents=True,exist_ok=True)
        def box(kind,data):return struct.pack('>I4s',len(data)+8,kind)+data
        data=box(b'ftyp',b'isom0000')+box(b'mdat',b'video-data'*50)
        if complete:data+=box(b'moov',b'complete-index')+b'\0'*32
        path=root/name;path.write_bytes(data);os.utime(path,(time.time()-30,time.time()-30))
        return path,data

    def record_post(self,action,**params):
        request=urllib.request.Request(f'http://127.0.0.1:{self.port}/api/rec/'+action,
            urllib.parse.urlencode(params).encode(),headers={'Content-Type':'application/x-www-form-urlencoded'})
        with urllib.request.urlopen(request,timeout=8) as r:return json.load(r)

    def record_status(self):
        with self.get('/api/rec/status') as r:return json.load(r)

    def test_native_record_status_uses_schedule_not_legacy_enable(self):
        status=self.record_status()
        self.assertTrue(status['status']['enabled'])
        self.assertTrue(status['status']['sd_ready'])
        self.assertFalse(status['status']['custom_schedule'])
        self.assertEqual(status['status']['segment_minutes'],5)

    def test_native_record_stop_start_requires_token_and_readback(self):
        token=self.record_status()['token']
        self.assertTrue(self.record_post('stop',token=token)['ok'])
        self.assertFalse(self.record_status()['status']['enabled'])
        self.assertTrue(self.record_post('start',token=token)['ok'])
        self.peer.record_write_error=True
        self.assertFalse(self.record_post('stop',token=token)['ok'])
        with self.assertRaises(urllib.error.HTTPError) as e:self.record_post('stop',token='wrong')
        self.assertEqual(e.exception.code,403)

    def test_long_wifi_request_preserves_type_and_24bit_length(self):
        ssid=' '*32;password='&'*63
        self.config.write_text('SSID="'+ssid+'"\nPWD="'+password+'"\nWIFI_APPLY=1\n')
        self.assertTrue(self.api('wifi_apply')['ok'])
        action,params=next((a,p) for a,p in self.peer.calls if a=='set_wifi')
        self.assertEqual(params['ssid'],[ssid]);self.assertEqual(params['wpa_psk'],[password])
        packets=[p for _,p in self.peer.control_packets if b'/set_wifi.cgi?' in p]
        self.assertGreater(len(packets[-1])-12,255)
        self.assertEqual(packets[-1][6:8],b'\0\0')
        self.assertEqual(int.from_bytes(packets[-1][8:11],'little'),len(packets[-1])-12)

    def test_native_record_custom_schedule_preserved(self):
        self.peer.record_values[0]=16846848
        token=self.record_status()['token']
        self.assertFalse(self.record_post('stop',token=token)['ok'])
        self.assertEqual(self.peer.record_values[0],16846848)
        self.assertFalse(any(a=='trans_cmd_string' and p.get('command')==['3'] for a,p in self.peer.calls))

    def test_exporter_uses_current_native_identity_and_current_config(self):
        self.stop_gateway();self.config.write_text('RECORD_TARGET=ftp\nCAM_ID=another-logical-name\n')
        marker=self.folder/'export-called.json';helper=self.folder/'worker'
        helper.write_text('#!/usr/bin/env python3\nimport sys,json,time\nfrom pathlib import Path\n'+f'Path({str(marker)!r}).write_text(json.dumps(sys.argv[1:]))\n'+f'Path({str(self.folder/"record-export.json")!r}).write_text(\'{{"phase":"idle","error":""}}\')\n'+'time.sleep(30)\n');helper.chmod(0o755)
        self.extra_args=['--record-exports','--record-worker-bin',str(helper),'--record-store-bin','/qc5/bin/qc5-store'];self.start_gateway()
        end=time.monotonic()+4
        while not marker.exists() and time.monotonic()<end:time.sleep(.02)
        args=json.loads(marker.read_text());self.assertEqual(args[args.index('--serial')+1],self.peer.identity)
        self.assertEqual(args[args.index('--config')+1],str(self.config))
        status=self.record_status()['status']['exporter'];self.assertTrue(status['available']);self.assertTrue(status['running']);self.assertEqual(status['phase'],'idle')

    def test_sd_destination_does_not_launch_exporter(self):
        self.stop_gateway();self.config.write_text('RECORD_TARGET=sd\n')
        marker=self.folder/'bad-export';helper=self.folder/'worker';helper.write_text('#!/bin/sh\ntouch '+str(marker)+'\n');helper.chmod(0o755)
        self.extra_args=['--record-exports','--record-worker-bin',str(helper)];self.start_gateway()
        self.assertFalse(self.record_status()['status']['exporter']['running']);self.assertFalse(marker.exists())

    def test_record_list_confined_to_current_camera_and_complete_segments(self):
        self.record_file();self.record_file(name='110000_100.mp4')
        self.record_file(name='130000_100.mp4',complete=False)
        self.record_file(identity='VE0000001OTHER')
        external=self.folder/'private.mp4';external.write_bytes(b'private')
        (self.folder/self.peer.identity/'video'/'2026-10-26'/'140000_100.mp4').symlink_to(external)
        with self.get('/api/rec/list') as r:files=json.load(r)['files']
        self.assertEqual([f['name'] for f in files],['2026-10-26/120000_100.mp4','2026-10-26/110000_100.mp4'])

    def test_record_download_ranges_and_invalid_paths(self):
        path,data=self.record_file();url='/api/rec/download?'+urllib.parse.urlencode({'file':'2026-10-26/'+path.name})
        for value,expected in [('bytes=0-15',data[:16]),('bytes=-24',data[-24:]),('bytes=24-',data[24:])]:
            request=urllib.request.Request(f'http://127.0.0.1:{self.port}'+url,headers={'Range':value})
            for attempt in range(20):
                try:
                    with urllib.request.urlopen(request,timeout=8) as r:
                        self.assertEqual(r.status,206);self.assertEqual(r.read(),expected)
                    break
                except urllib.error.HTTPError as e:
                    if e.code!=503 or attempt==19:raise
                    time.sleep(.05)
        for value in ['bytes=999999999999999999999999-','bytes=3-1','bytes=0-1,4-5']:
            request=urllib.request.Request(f'http://127.0.0.1:{self.port}'+url,headers={'Range':value})
            with self.assertRaises(urllib.error.HTTPError) as e:urllib.request.urlopen(request,timeout=8)
            self.assertEqual(e.exception.code,416)
        with self.assertRaises(urllib.error.HTTPError) as e:self.get('/api/rec/download?file=../../config.txt')
        self.assertEqual(e.exception.code,404)

    def test_record_reader_waits_for_memory_before_opening_video(self):
        self.stop_gateway();self.memory_file=self.folder/'memory'
        self.memory_file.write_text('MemAvailable: 1200 kB\n');self.start_gateway()
        with self.get('/api/rec/ready') as r:self.assertFalse(json.load(r)['ready'])
        self.memory_file.write_text('MemAvailable: 4096 kB\n')
        with self.get('/api/rec/ready') as r:self.assertTrue(json.load(r)['ready'])

    def test_record_reader_reserves_more_than_two_mib_before_fork(self):
        path,_=self.record_file()
        self.stop_gateway();self.memory_file=self.folder/'memory'
        self.memory_file.write_text('MemAvailable: 2600 kB\n');self.start_gateway()
        with self.get('/api/rec/ready') as r:self.assertFalse(json.load(r)['ready'])
        with self.assertRaises(urllib.error.HTTPError)as e:self.get('/api/rec/download?file=2026-10-26/'+path.name)
        self.assertEqual(e.exception.code,503)

    def test_record_download_stops_on_memory_pressure_without_stopping_controls(self):
        path,_=self.record_file();payload=32*1024*1024
        with path.open('wb')as f:
            f.write(struct.pack('>I4s',16,b'ftyp')+b'isom0000')
            f.write(struct.pack('>I4s',payload+8,b'mdat'));f.seek(payload,1)
            f.write(struct.pack('>I4s',8,b'moov'))
        os.utime(path,(time.time()-30,time.time()-30));size=path.stat().st_size
        self.stop_gateway();self.memory_file=self.folder/'memory'
        self.memory_file.write_text('MemAvailable: 4096 kB\n');self.start_gateway()
        sock=socket.socket();sock.setsockopt(socket.SOL_SOCKET,socket.SO_RCVBUF,4096);sock.settimeout(5)
        self.addCleanup(sock.close);sock.connect(('127.0.0.1',self.port))
        sock.sendall(('GET /api/rec/download?file=2026-10-26/'+path.name+' HTTP/1.1\r\nHost: localhost\r\n\r\n').encode())
        header=b''
        while b'\r\n\r\n'not in header:header+=sock.recv(1)
        self.assertIn(b'200 OK',header)
        self.memory_file.write_text('MemAvailable: 768 kB\n')
        time.sleep(.4)
        with self.get('/api/rec/ready')as r:self.assertFalse(json.load(r)['ready'])
        got=0
        while True:
            chunk=sock.recv(65536)
            if not chunk:break
            got+=len(chunk)
        self.assertLess(got,size)
        self.memory_file.write_text('MemAvailable: 4096 kB\n')
        end=time.monotonic()+3;ready=False
        while time.monotonic()<end:
            with self.get('/api/rec/ready')as r:ready=json.load(r)['ready']
            if ready:break
            time.sleep(.05)
        self.assertTrue(ready)

    def test_small_browser_probe_does_not_spawn_or_compete_with_downloads(self):
        path,data=self.record_file()
        self.stop_gateway();self.memory_file=self.folder/'memory'
        self.memory_file.write_text('MemAvailable: 1200 kB\n');self.start_gateway()
        url=f'http://127.0.0.1:{self.port}/api/rec/download?file=2026-10-26/'+path.name
        for _ in range(3):
            with urllib.request.urlopen(urllib.request.Request(url,headers={'Range':'bytes=0-1'}),timeout=5) as r:
                self.assertEqual(r.status,206);self.assertEqual(r.read(),data[:2])
        with self.assertRaises(urllib.error.HTTPError) as e:self.get('/api/rec/download?file=2026-10-26/'+path.name)
        self.assertEqual(e.exception.code,503)

    def test_record_nested_native_sensor_layout(self):
        path,data=self.record_file()
        sensor=path.parent/'s0';sensor.mkdir();path.rename(sensor/path.name)
        token=self.record_status()['token']
        with self.get('/api/rec/list') as r:files=json.load(r)['files']
        self.assertEqual(len(files),1)
        with self.get('/api/rec/download?file='+files[0]['name']) as r:self.assertEqual(r.read(),data)
        self.assertTrue(self.record_post('delete',token=token,file=files[0]['name'])['ok'])
        self.assertFalse((sensor/path.name).exists())

    def test_record_symlink_sensor_cannot_escape_sd(self):
        path,_=self.record_file()
        external=self.folder/'outside';external.mkdir()
        (path.parent/'s0').symlink_to(external,target_is_directory=True)
        token=self.record_status()['token']
        self.assertFalse(self.record_post('delete',token=token,file='2026-10-26/'+path.name)['ok'])
        self.assertTrue(path.exists())

    def test_record_delete_is_post_only_and_requires_closed_regular_file(self):
        path,_=self.record_file();partial,_=self.record_file(name='130000_100.mp4',complete=False)
        token=self.record_status()['token']
        with self.assertRaises(urllib.error.HTTPError) as e:self.get('/api/rec/delete?file=2026-10-26/'+path.name)
        self.assertEqual(e.exception.code,405)
        self.assertFalse(self.record_post('delete',token=token,file='2026-10-26/'+partial.name)['ok'])
        self.assertTrue(partial.exists())
        self.assertTrue(self.record_post('delete',token=token,file='2026-10-26/'+path.name)['ok'])
        self.assertFalse(path.exists())

if __name__ == '__main__':
    unittest.main(verbosity=2)
