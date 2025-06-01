import asyncio
import json
import os
import msgpack
import array

import websockets
import zmq


async def receive_messages(websocket, socket):
    try:
        async for message in websocket:
            try:
                data = msgpack.unpackb(message, raw=False)

                if data["T"] == "success":
                    print(f"Connection/Authentication successful. {data}")
                elif data["T"] == "subscription":
                    print(f"Subscription successful: {data}")
                elif data["T"] == "error":
                    print(f"Error received: {data}")
                else:  # Forward the message to the ZeroMQ socket
                    socket.send(data, copy=False, flags=zmq.DONTWAIT)
                    print(f"Forwarded message to ZeroMQ socket: {message}")
            except json.JSONDecodeError:
                print("Received message is not valid JSON.")
            except zmq.Again:
                print(f"ZMQ Again - not ready to send message: {message}")
            except zmq.ZMQError as e:
                print(f"ZMQ Error: {e}")
    except websockets.exceptions.ConnectionClosedOK:
        print("Receive: WebSocket connection closed gracefully.")
    except websockets.exceptions.ConnectionClosedError as e:
        print(f"Receive: WebSocket connection closed with error: {e}")
    except Exception as e:
        print(f"Receive: An unexpected error occurred: {e}")
        websocket.close()


async def send_messages(websocket):
    try:
        api_key = os.environ.get("APCA_API_KEY_ID")
        api_secret = os.environ.get("APCA_API_SECRET_KEY")
        auth: str = json.dumps({"action": "auth", "key": api_key, "secret": api_secret})
        await websocket.send(auth)

        subscription = json.dumps(
            {
                "action": "subscribe",
                "trades": ["AAPL", "GOOGL", "AMZN"],
                "quotes": ["SPY", "QQQ"],
            }
        )
        await websocket.send(subscription)
    except websockets.exceptions.ConnectionClosedOK:
        print("Send: WebSocket connection closed gracefully.")
    except websockets.exceptions.ConnectionClosedError as e:
        print(f"Send: WebSocket connection closed with error: {e}")
    except Exception as e:
        print(f"Send: An unexpected error occurred: {e}")
        websocket.close()


async def main():
    uri = "wss://stream.data.alpaca.markets/v2/iex"
    context = zmq.Context()
    socket = context.socket(zmq.PUSH)
    socket.setsockopt(zmq.SNDHWM, 0)
    socket.setsockopt(zmq.IMMEDIATE, 1)
    socket.setsockopt(zmq.AFFINITY, 1)
    socket.connect("inproc://zmq_push")
    headers = {"Content-Type": "application/msgpack"}

    try:
        async with websockets.connect(uri, additional_headers=headers) as websocket:
            await asyncio.gather(
                receive_messages(websocket, socket), send_messages(websocket)
            )
    except KeyboardInterrupt:
        print("Connection closed by user.")
        socket.close()
        context.term()
    except Exception as e:
        print(f"Main connection error: {e}")


if __name__ == "__main__":
    asyncio.run(main())
