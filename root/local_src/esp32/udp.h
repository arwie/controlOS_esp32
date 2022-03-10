// Copyright (c) 2022 Artur Wiebe <artur@4wiebe.de>
//
// Permission is hereby granted, free of charge, to any person obtaining a copy of this software and
// associated documentation files (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
// IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
// WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.


class UdpSender
{
public:
	void init(const char* receiverAddr, const int receiverPort)
	{
		addr.sin_addr.s_addr = inet_addr(receiverAddr);
		addr.sin_port = htons(receiverPort);
		addr.sin_family = AF_INET;
		
		sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
		if (sock < 0)
			ESP_LOGE(TAG, "UdpSender:init > errno %d", errno);
	}
	
	void sendJson(const JsonDocument& data)
	{
		char msg[512];
		auto msgLen = serializeJson(data, msg, sizeof(msg));
		if (sendto(sock, msg, msgLen, 0, (struct sockaddr *)&addr, sizeof(addr)) < 0)
			ESP_LOGE(TAG, "UdpSender:sendJson > errno %d", errno);
	}
	
private:
	struct sockaddr_in addr;
	int sock;
};
