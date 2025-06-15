import unittest
import asyncio
import msgpack
from unittest.mock import AsyncMock

import publisher


class TestPublisher(unittest.TestCase):

    def setUp(self):
        self.MARKET_EVENT_STRUCT = publisher.MARKET_EVENT
        self.mock_zmq_socket = AsyncMock()

    def test_handle_valid_quote(self):
        sample_quote = {
            "T": "q",
            "S": "AAPL",
            "t": 1678886400000000000,
            "bp": 150.50,
            "bs": 100,
            "ap": 150.55,
            "as": 200,
        }
        message = msgpack.packb([sample_quote])

        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))

        self.mock_zmq_socket.send_multipart.assert_called_once()
        call_args = self.mock_zmq_socket.send_multipart.call_args[0][0]
        sent_topic, sent_payload = call_args[0], call_args[1]
        self.assertEqual(sent_topic, b"AAPL")
        unpacked_data = self.MARKET_EVENT_STRUCT.unpack(sent_payload)
        self.assertEqual(unpacked_data[0], 1)
        self.assertAlmostEqual(unpacked_data[2], 150.50)

    def test_handle_valid_trade(self):
        sample_trade = {
            "T": "t",
            "S": "GOOGL",
            "t": 1678886401000000000,
            "p": 2800.75,
            "s": 50,
        }
        message = msgpack.packb([sample_trade])

        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))

        self.mock_zmq_socket.send_multipart.assert_called_once()
        call_args = self.mock_zmq_socket.send_multipart.call_args[0][0]
        unpacked_data = self.MARKET_EVENT_STRUCT.unpack(call_args[1])
        self.assertEqual(unpacked_data[0], 2)

    def test_handle_malformed_data_is_ignored(self):
        """Tests if messages missing key fields are gracefully ignored."""
        bad_message = {"T": "q", "t": 123}
        message = msgpack.packb([bad_message])

        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))

        self.mock_zmq_socket.send_multipart.assert_not_called()

    def test_successful_run_loop(self):
        mock_websocket = AsyncMock()
        auth_success_msg = msgpack.packb([{"T": "success", "msg": "authenticated"}])
        sub_success_msg = msgpack.packb([{"T": "success", "msg": "subscribed"}])
        mock_websocket.recv.side_effect = [auth_success_msg, sub_success_msg]
        sample_quote = {"T": "q", "S": "AAPL", "t": 12345}
        market_data_messages = [msgpack.packb([sample_quote])]
        mock_websocket.__aiter__.return_value = (msg for msg in market_data_messages)

        asyncio.run(
            publisher.run_communication_loop(mock_websocket, self.mock_zmq_socket)
        )

        self.assertEqual(mock_websocket.send.call_count, 2)
        self.assertEqual(mock_websocket.recv.call_count, 2)
        auth_call_data = msgpack.unpackb(mock_websocket.send.call_args_list[0].args[0])
        self.assertEqual(auth_call_data["action"], "auth")
        sub_call_data = msgpack.unpackb(mock_websocket.send.call_args_list[1].args[0])
        self.assertEqual(sub_call_data["action"], "subscribe")
        self.mock_zmq_socket.send_multipart.assert_called_once()

    def test_authentication_failure(self):
        mock_websocket = AsyncMock()

        auth_failure_msg = msgpack.packb([{"T": "error", "msg": "auth failed"}])
        mock_websocket.recv.return_value = auth_failure_msg

        asyncio.run(
            publisher.run_communication_loop(mock_websocket, self.mock_zmq_socket)
        )

        self.assertEqual(mock_websocket.send.call_count, 1)
        self.assertEqual(mock_websocket.recv.call_count, 1)
        self.mock_zmq_socket.send_multipart.assert_not_called()

    def test_authentication_exception(self):
        mock_websocket = AsyncMock()
        mock_websocket.send.side_effect = ConnectionError("Connection lost")

        asyncio.run(
            publisher.run_communication_loop(mock_websocket, self.mock_zmq_socket)
        )

        self.mock_zmq_socket.send_multipart.assert_not_called()
        self.assertEqual(mock_websocket.recv.call_count, 0)


if __name__ == "__main__":
    unittest.main()
