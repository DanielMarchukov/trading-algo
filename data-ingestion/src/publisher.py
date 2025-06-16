import asyncio
import os
import msgpack
import struct
import websockets
import zmq
import zmq.asyncio
import sys
import time

# Format
# B: 1-byte uint (EventType: 1=Quote, 2=Trade)
# Q: 8-byte ulonglong (Timestamp)
# d: 8-byte double (Price1)
# I: 4-byte uint (Size1)
# d: 8-byte double (Price2)
# I: 4-byte uint (Size2)
# Q: 8-byte ulonglong (arrived_at nanos ts)
# 7x: 7 bytes of padding to make the total size 40 bytes.
MARKET_EVENT = struct.Struct("<BQdIdIQ7x")
assert MARKET_EVENT.size == 48, "Struct size mismatch"


async def handle_market_data(message, zmq_socket):
    try:
        arrived_at = time.time_ns()
        data_list = msgpack.unpackb(message, raw=False)
        if not isinstance(data_list, list) or not data_list:
            return
        for item in data_list:
            msg_type = item.get("T")
            symbol = item.get("S")

            if not msg_type or not symbol:
                continue

            topic = symbol.encode("utf-8")
            packed_data = None
            if msg_type == "q":
                event_type = 1
                timestamp = item.get("t", 0)
                bid_price = item.get("bp", 0.0)
                bid_size = item.get("bs", 0)
                ask_price = item.get("ap", 0.0)
                ask_size = item.get("as", 0)

                packed_data = MARKET_EVENT.pack(
                    event_type,
                    timestamp,
                    bid_price,
                    bid_size,
                    ask_price,
                    ask_size,
                    arrived_at,
                )

            elif msg_type == "t":
                event_type = 2
                timestamp = item.get("t", 0)
                trade_price = item.get("p", 0.0)
                trade_size = item.get("s", 0)

                packed_data = MARKET_EVENT.pack(
                    event_type,
                    timestamp,
                    trade_price,
                    trade_size,
                    0.0,
                    0,
                    arrived_at,
                )

            if packed_data:
                await zmq_socket.send_multipart([topic, packed_data])
    except (
        msgpack.UnpackException,
        msgpack.ExtraData,
        websockets.ConnectionClosedError,
        IndexError,
        AttributeError,
    ) as e:
        print(f"Caught exception: {e}")


async def run_communication_loop(websocket, zmq_socket):
    try:
        api_key = os.environ.get("APCA_API_KEY_ID")
        api_secret = os.environ.get("APCA_API_SECRET_KEY")
        if not api_key or not api_secret:
            raise ValueError(
                "APCA_API_SECRET_KEY and APCA_API_KEY_ID must be set in environment variables."
            )

        auth = msgpack.packb({"action": "auth", "key": api_key, "secret": api_secret})
        await websocket.send(auth)
        auth_response_msg = await websocket.recv()
        auth_response = msgpack.unpackb(auth_response_msg, raw=False)
        if (
            not isinstance(auth_response, list)
            or not auth_response
            or auth_response[0].get("T") != "success"
        ):
            print(f"FATAL: Authentication failed: {auth_response}")
            return
        print(f"Authentication response: {auth_response}")

        subscription = msgpack.packb(
            {"action": "subscribe", "quotes": ["AAPL", "GOOGL", "AMZN"]}
        )
        await websocket.send(subscription)
        subscription_response_msg = await websocket.recv()
        subscription_response = msgpack.unpackb(subscription_response_msg, raw=False)
        if (
            not isinstance(subscription_response, list)
            or subscription_response[0].get("T") != "success"
        ):
            print(f"FATAL: Subscription failed: {subscription_response}")
            return
        print(f"Subscription response: {subscription_response}")

        async for message in websocket:
            await handle_market_data(message, zmq_socket)
    except Exception as e:
        print(f"Send: An unexpected error occurred: {e}")
        return


async def main(zmq_address):
    uri = "wss://stream.data.alpaca.markets/v2/iex"
    headers = {"Content-Type": "application/msgpack"}
    context = zmq.asyncio.Context()
    zmq_socket = context.socket(zmq.PUB)
    zmq_socket.setsockopt(zmq.SNDHWM, 1_000_000)
    zmq_socket.setsockopt(zmq.LINGER, 0)
    zmq_socket.bind(zmq_address)

    try:
        async with websockets.connect(uri, additional_headers=headers) as websocket:
            await run_communication_loop(websocket, zmq_socket)
    except (KeyboardInterrupt, Exception) as e:
        print(f"Main connection error: {e}")
    finally:
        zmq_socket.close()
        context.term()


if __name__ == "__main__":
    if sys.platform.startswith("linux"):
        try:
            os.sched_setaffinity(0, {0})
        except AttributeError:
            print("Cannot set CPU affinity")

    ipc_socket = "ipc:///tmp/market_data.sock"
    asyncio.run(main(ipc_socket))
