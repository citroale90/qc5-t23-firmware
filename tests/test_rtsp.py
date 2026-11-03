"""The real gateway RTSP endpoint against a deterministic native-camera peer."""
import json
import socket
import struct
import time
import unittest
import test_gateway as fixture


def native(kind,data,ms):
    h=bytearray(32);h[:4]=bytes.fromhex('55aa15a8');h[4]=kind
    struct.pack_into('<H',h,6,ms%1000);struct.pack_into('<I',h,8,ms//1000);struct.pack_into('<I',h,16,len(data))
    return h+data


class Client:
    def __init__(self,port):
        self.socket=socket.create_connection(('127.0.0.1',port),timeout=6);self.socket.settimeout(7)
        self.buffer=b'';self.seq=0;self.session=None;self.base=f'rtsp://127.0.0.1:{port}/live';self.frames=[]

    def close(self):self.socket.close()

    def more(self):
        b=self.socket.recv(8192)
        if not b:raise EOFError()
        self.buffer+=b

    def message(self):
        while not self.buffer:self.more()
        if self.buffer[0]==36:
            while len(self.buffer)<4:self.more()
            channel=self.buffer[1];n=struct.unpack_from('>H',self.buffer,2)[0]
            while len(self.buffer)<n+4:self.more()
            data=self.buffer[4:n+4];self.buffer=self.buffer[n+4:];return channel,data
        while b'\r\n\r\n' not in self.buffer:self.more()
        end=self.buffer.index(b'\r\n\r\n')+4;head=self.buffer[:end].decode();headers={}
        for line in head.split('\r\n')[1:]:
            if ':' in line:k,v=line.split(':',1);headers[k.lower()]=v.strip()
        size=int(headers.get('content-length','0'))
        while len(self.buffer)<end+size:self.more()
        body=self.buffer[end:end+size];self.buffer=self.buffer[end+size:]
        return int(head.split(' ',2)[1]),headers,body

    def request(self,method,path='',headers=None,use_session=True):
        self.seq+=1;extra=dict(headers or {})
        if self.session and use_session:extra['Session']=self.session
        self.socket.sendall((f'{method} {self.base+path} RTSP/1.0\r\nCSeq: {self.seq}\r\n'+
                             ''.join(k+': '+v+'\r\n' for k,v in extra.items())+'\r\n').encode())
        while True:
            response=self.message()
            if len(response)==2:self.frames.append(response);continue
            if 'session' in response[1]:self.session=response[1]['session'].split(';')[0]
            return response

    def frame(self,channel):
        while True:
            message=self.message()
            if len(message)==2 and message[0]==channel:return message[1]


class RTSP(unittest.TestCase):
    @classmethod
    def setUpClass(cls):fixture.Gateway.setUpClass()
    @classmethod
    def tearDownClass(cls):fixture.Gateway.tearDownClass()

    def setUp(self):
        self.f=fixture.Gateway('test_white_light_readback');self.f.setUp();self.addCleanup(self.f.doCleanups)
        self.f.stop_gateway()
        with socket.socket() as s:s.bind(('127.0.0.1',0));self.port=s.getsockname()[1]
        self.f.extra_args=['--rtsp-port',str(self.port)];self.f.start_gateway();self.sequence=1
        self.f.peer.startup_media=native(0,b'\0\0\0\1\x67\x42\0\x1e\x80\0\0\0\1\x68\xce\x06\xe2\0\0\0\1\x65\x80',1000)+native(1,b'\0\0\0\1\x41\x80',1066)

    def client(self):
        c=Client(self.port);self.addCleanup(c.close);return c

    def prepare(self,audio=True):
        c=self.client();code,headers,sdp=c.request('DESCRIBE');self.assertEqual(code,200)
        self.assertIn(b'H264/90000',sdp);self.assertIn(b'PCMA/16000/1',sdp)
        self.assertEqual(headers['content-base'],c.base+'/')
        self.assertEqual(c.request('SETUP','/track0',{'Transport':'RTP/AVP;unicast;client_port=20000-20001'})[0],461)
        self.assertEqual(c.request('SETUP','/track0',{'Transport':'RTP/AVP/TCP;unicast;interleaved=0-1'})[0],200)
        if audio:self.assertEqual(c.request('SETUP','/track1',{'Transport':'RTP/AVP/TCP;unicast;interleaved=2-3'})[0],200)
        self.assertEqual(c.request('PLAY')[0],200);return c

    def send(self,data):
        for at in range(0,len(data),1000):
            payload=b'\xd1\x01'+struct.pack('>H',self.sequence)+data[at:at+1000];self.sequence+=1
            self.f.peer.send(0xd0,payload,self.f.peer.listen_address)
            time.sleep(.002)

    def test_options_does_not_start_an_encoder_or_native_session(self):
        c=self.client();code,headers,_=c.request('OPTIONS');self.assertEqual(code,200)
        self.assertIn('DESCRIBE',headers['public']);self.assertFalse(self.f.peer.live_sessions)

    def test_teardown_drains_inflight_native_media_and_confirms_stop(self):
        self.f.peer.stop_waits_for_media_ack=True
        c=self.prepare();address=next(iter(self.f.peer.live_sessions))
        self.assertEqual(c.request('TEARDOWN')[0],200)
        until=time.monotonic()+1.5
        while address not in self.f.peer.stop_confirmations and time.monotonic()<until:time.sleep(.01)
        self.assertIn(address,self.f.peer.stop_confirmations)
        self.assertFalse(self.f.peer.live_sessions)

    def test_rtp_video_and_variable_audio_packets_use_the_negotiated_channels(self):
        c=self.prepare();self.send(native(0,b'\0\0\0\1\x65\x99',1132)+native(1,b'\0\0\0\1\x41\x99',1198)+native(12,b'\xd5'*320,1198))
        video=c.frame(0);self.assertEqual(video[1]&127,96);self.assertEqual(video[0],128)
        audio=c.frame(2);self.assertEqual(audio[1]&127,97);self.assertEqual(audio[12:],b'\xd5'*320)
        first=struct.unpack_from('>I',audio,4)[0];self.send(native(12,b'\xd5'*640,1172));second=c.frame(2)
        self.assertEqual(struct.unpack_from('>I',second,4)[0]-first,320)
        self.assertNotEqual(video[8:12],audio[8:12])
        self.assertEqual(c.request('TEARDOWN')[0],200)

    def test_large_h264_nal_roundtrips_through_fu_a(self):
        c=self.prepare(audio=False);nal=b'\x65'+bytes(range(1,251))*16
        self.send(native(0,b'\0\0\0\1'+nal,1132)+native(1,b'\0\0\0\1\x41\x99',1198))
        rebuilt=bytearray();started=False
        for _ in range(12):
            packet=c.frame(0);p=packet[12:]
            if p[0]&31!=28:continue
            if p[1]&128:rebuilt.append((p[0]&0xe0)|(p[1]&31));started=True
            self.assertTrue(started);rebuilt.extend(p[2:])
            if p[1]&64:self.assertTrue(packet[1]&128);break
        self.assertEqual(rebuilt,nal)

    def test_rtp_clock_ignores_doubled_or_jumping_vendor_frame_time(self):
        c=self.prepare(audio=False)
        self.send(native(0,b'\0\0\0\1\x65\x99',1132)+native(1,b'\0\0\0\1\x41\x99',1198))
        first=c.frame(0);start=time.monotonic();time.sleep(.15)
        # A 20-second jump in the native timestamp must not schedule pictures
        # twenty seconds in the future relative to the RTCP/audio reference.
        self.send(native(1,b'\0\0\0\1\x41\x99',21198)+native(1,b'\0\0\0\1\x41\x99',21264))
        next_packet=c.frame(0);real=time.monotonic()-start
        delta=((struct.unpack_from('>I',next_packet,4)[0]-struct.unpack_from('>I',first,4)[0])&0xffffffff)/90000
        self.assertGreater(delta,.08);self.assertLess(abs(delta-real),.25)

    def test_sessions_paths_and_overlapping_channels_are_rejected(self):
        c=self.prepare()
        self.assertEqual(c.request('PLAY',use_session=False)[0],454)
        self.assertEqual(c.request('PAUSE',headers={'Session':'foreign'},use_session=False)[0],454)
        self.assertEqual(c.request('PAUSE')[0],200)
        self.assertEqual(c.request('SETUP','/track1',{'Transport':'RTP/AVP/TCP;unicast;interleaved=0-1'})[0],461)
        self.assertEqual(c.request('DESCRIBE','/../../config.txt')[0],404)
        c.session='foreign';self.assertEqual(c.request('PLAY')[0],454)

    def test_stream_address_uses_the_active_camera_listener(self):
        self.f.config.write_text('RTSP_URL="rtsp://retired-nas:8554/other-unit"\n')
        with self.f.get('/cgi-bin/webui?command=sysinfo') as r:info=json.load(r)
        self.assertEqual(info['RTSP_URL'],f'rtsp://{info["ip"]}:{self.port}/live')
        self.assertNotIn('retired-nas',info['RTSP_URL'])

    def test_low_memory_refuses_only_the_stream_and_keeps_web_controls(self):
        self.f.stop_gateway();self.f.memory_file=self.f.folder/'meminfo';self.f.memory_file.write_text('MemAvailable: 1024 kB\n');self.f.start_gateway()
        c=self.client();self.assertEqual(c.message()[0],503)
        with self.f.get('/cgi-bin/webui?command=Woff') as r:self.assertTrue(json.load(r)['ok'])
        self.assertFalse(self.f.peer.live_sessions)

    def test_active_stream_releases_native_encoder_before_memory_is_exhausted(self):
        self.f.stop_gateway();self.f.memory_file=self.f.folder/'meminfo'
        self.f.memory_file.write_text('MemAvailable: 4096 kB\n');self.f.start_gateway()
        c=self.prepare();self.f.memory_file.write_text('MemAvailable: 1023 kB\n')
        started=time.monotonic();c.socket.settimeout(2)
        with self.assertRaises(EOFError):
            while True:c.message()
        self.assertLess(time.monotonic()-started,1.5)
        until=time.monotonic()+1.5
        while self.f.peer.live_sessions and time.monotonic()<until:time.sleep(.01)
        self.assertFalse(self.f.peer.live_sessions)
        with self.f.get('/cgi-bin/webui?command=Woff') as r:self.assertTrue(json.load(r)['ok'])

    def test_sender_reports_share_the_rtp_ssrc_and_correct_packet_counts(self):
        c=self.prepare();self.send(native(0,b'\0\0\0\1\x65\x99',1132)+native(1,b'\0\0\0\1\x41\x99',1198)+native(12,b'\xd5'*640,1198))
        video=c.frame(0);audio=c.frame(2);v=c.frame(1);a=c.frame(3)
        self.assertEqual(v[:2],b'\x80\xc8');self.assertEqual(a[:2],b'\x80\xc8')
        self.assertEqual(v[4:8],video[8:12]);self.assertEqual(a[4:8],audio[8:12]);self.assertIn(b'qc5-local',v)
        self.assertEqual(struct.unpack_from('>I',a,20)[0],1);self.assertEqual(struct.unpack_from('>I',a,24)[0],640)
        self.assertEqual(v[8:16],a[8:16])
        self.assertLess(abs(struct.unpack_from('>I',v,16)[0]/90000-struct.unpack_from('>I',a,16)[0]/16000),.001)


if __name__=='__main__':unittest.main()
