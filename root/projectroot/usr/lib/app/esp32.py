#!/usr/bin/python -Bu

# Copyright (c) 2022 Artur Wiebe <artur@4wiebe.de>
#
# Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
# associated documentation files (the "Software"), to deal in the Software without restriction,
# including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
# and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
# subject to the following conditions:
#
# The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
#
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
# INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
# IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
# WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


import logging, asyncio, json


class UdpMaster(asyncio.DatagramProtocol):
	def __init__(self, host, port, cycle_time):
		self._address = (host, port)
		self._cycle_time = cycle_time

		self.connected = False
		self.cmd = {}
		self.fbk = {}

	def connection_made(self, transport):
		pass

	def datagram_received(self, data, addr):
		self.fbk.update(json.loads(data.decode()))
		self.connected = True

	async def execute(self):
		transport, protocol = await asyncio.get_running_loop().create_datagram_endpoint(lambda:self, remote_addr=self._address)
		while (True):
			await asyncio.sleep(self._cycle_time)
			transport.sendto(json.dumps(self.cmd).encode())

	def start(self):
		asyncio.create_task(self.execute())
