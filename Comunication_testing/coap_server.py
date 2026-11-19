# coap_server.py - a small CoAP server for testing: prints every message sent to /hello
#   pip install aiocoap
#   python coap_server.py
import asyncio
import aiocoap
import aiocoap.resource as resource


class Hello(resource.Resource):
    """The resource coap://<this PC>/hello"""

    async def render_post(self, request):
        sender = request.remote.hostinfo                 # IP:port of the ESP32
        kind = "CON" if request.mtype == aiocoap.CON else "NON"
        print(f"{kind} from {sender}: {request.payload.decode()}")
        return aiocoap.Message(code=aiocoap.CHANGED)     # answer 2.04 "Changed" = received


async def main():
    site = resource.Site()
    site.add_resource(["hello"], Hello())
    # "::" = listen on all network addresses (IPv4 and IPv6), port 5683 = CoAP's standard port
    await aiocoap.Context.create_server_context(site, bind=("::", 5683))
    print("CoAP server running on port 5683 - waiting for messages on /hello ...")
    await asyncio.get_running_loop().create_future()     # keep running until Ctrl+C


asyncio.run(main())