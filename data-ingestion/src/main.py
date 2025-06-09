import asyncio
import os
import msgpack

import websockets
import zmq


async def receive_messages(websocket, socket):
    try:
        async for message in websocket:
            data = msgpack.unpackb(message, raw=False)
            print(f"data: {data}")
            # socket.send(data, copy=False, flags=zmq.DONTWAIT)
    except Exception as e:
        print(f"Error {e}")
        await websocket.close()


async def send_messages(websocket):
    try:
        api_key = os.environ.get("APCA_API_KEY_ID")
        api_secret = os.environ.get("APCA_API_SECRET_KEY")
        auth = msgpack.packb({"action": "auth", "key": api_key, "secret": api_secret})
        await websocket.send(auth)

        subscription = msgpack.packb(
            {"action": "subscribe", "quotes": ["AAPL", "GOOGL", "AMZN"]}
        )
        await websocket.send(subscription)
    except Exception as e:
        print(f"Send: An unexpected error occurred: {e}")
        await websocket.close()


async def init():
    uri = "wss://stream.data.alpaca.markets/v2/iex"
    context = zmq.Context()
    socket = context.socket(zmq.PUSH)
    socket.setsockopt(zmq.SNDHWM, 0)
    socket.setsockopt(zmq.IMMEDIATE, 1)
    socket.setsockopt(zmq.AFFINITY, 1)
    socket.connect("inproc://alpaca_channel")
    headers = {"Content-Type": "application/msgpack"}

    try:
        async with websockets.connect(uri, additional_headers=headers) as websocket:
            await asyncio.gather(
                receive_messages(websocket, socket), send_messages(websocket)
            )
    except KeyboardInterrupt or Exception as e:
        print(f"Main connection error: {e}")
        socket.close()
        context.term()


if __name__ == "__main__":
    asyncio.run(init())
