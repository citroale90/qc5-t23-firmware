"""Alarm-channel backpressure and late record replies, no camera hardware."""
import json,unittest
import test_gateway as fixture

class ControlRecovery(unittest.TestCase):
 @classmethod
 def setUpClass(cls):fixture.Gateway.setUpClass()
 @classmethod
 def tearDownClass(cls):fixture.Gateway.tearDownClass()
 def setUp(self):
  self.f=fixture.Gateway('test_white_light_readback');self.f.setUp();self.addCleanup(self.f.doCleanups)
 def test_unused_reliable_notifications_cannot_block_control_replies(self):
  original=self.f.peer.control_reply;pending={};sent=[]
  def reply(address,code,answer):
   if not sent:
    sent.append(True);pending[address]=(code,answer)
    self.f.peer.send(0xd0,b'\xd1\x02\x12\x34PRIVATE_UNUSED_ALARM',address)
   else:original(address,code,answer)
  def ack(address,body):
   if body==b'\xd1\x02\x00\x01\x12\x34'and address in pending:
    code,answer=pending.pop(address);original(address,code,answer)
  self.f.peer.control_reply=reply;self.f.peer.on_data_ack=ack
  data=self.f.api('sysinfo');self.assertTrue(data['adapter']['ok'])
  self.assertFalse(pending);self.assertIn(b'\xd1\x02\x00\x01\x12\x34',self.f.peer.data_acks)
  self.assertNotIn('PRIVATE_UNUSED_ALARM',json.dumps(data))
 def test_late_record_write_ack_cannot_replace_a_schedule_read(self):
  original=self.f.peer.control_reply;sent=[]
  def reply(address,code,answer):
   if code==0x60d1 and not sent:
    sent.append(True);original(address,code,b'var cmd=2017;var command=3;var status=0;')
   original(address,code,answer)
  self.f.peer.control_reply=reply
  self.assertTrue(self.f.record_status()['ok']);self.assertTrue(sent)
 def test_late_reply_from_another_schedule_cannot_replace_the_requested_type(self):
  original=self.f.peer.control_reply;sent=[]
  def reply(address,code,answer):
   if code==0x60d1 and not sent:
    sent.append(True);original(address,code,answer.replace(b'var type=3;',b'var type=2;'))
   original(address,code,answer)
  self.f.peer.control_reply=reply
  self.assertTrue(self.f.record_status()['ok']);self.assertTrue(sent)

if __name__=='__main__':unittest.main(verbosity=2)
