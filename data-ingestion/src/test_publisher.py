import asyncio
import msgpack
import unittest
from unittest.mock import AsyncMock, patch

import publisher


class TestPublisher(unittest.TestCase):

    def setUp(self):
        self.mock_zmq_socket = AsyncMock()

    @patch("time.time_ns", return_value=1234567890)
    def test_handle_valid_quote(self, _):
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
        self.assertEqual(len(sent_payload), 40)

        unpacked_data = publisher.MARKET_EVENT.unpack(sent_payload)
        self.assertEqual(unpacked_data[0], 1)
        self.assertEqual(unpacked_data[1], b"AAPL\0\0\0")
        self.assertEqual(unpacked_data[2], sample_quote["t"])
        self.assertEqual(
            unpacked_data[3], sample_quote["bp"] * publisher.SCALING_FACTOR
        )
        self.assertEqual(unpacked_data[7], 1234567890)

    @patch("time.time_ns", return_value=9876543210)
    def test_handle_valid_trade(self, _):
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
        unpacked_data = publisher.MARKET_EVENT.unpack(
            self.mock_zmq_socket.send_multipart.call_args[0][0][1]
        )
        self.assertEqual(unpacked_data[0], 2)
        self.assertEqual(unpacked_data[1], b"GOOGL\0\0")
        self.assertEqual(unpacked_data[3], sample_trade["p"] * publisher.SCALING_FACTOR)
        self.assertEqual(unpacked_data[7], 9876543210)

    def test_handle_malformed_data_is_ignored(self):
        bad_message = {"T": "q", "t": 123}
        message = msgpack.packb([bad_message])

        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))

        self.mock_zmq_socket.send_multipart.assert_not_called()

    def test_successful_run_loop(self):
        mock_websocket = AsyncMock()
        auth_success = msgpack.packb([{"T": "success", "msg": "authenticated"}])
        sub_success = msgpack.packb([{"T": "success", "msg": "subscribed"}])
        mock_websocket.recv.side_effect = [auth_success, sub_success]

        sample_quote = {"T": "q", "S": "AAPL", "t": 12345}
        mock_websocket.__aiter__.return_value = (
            msg for msg in [msgpack.packb([sample_quote])]
        )

        asyncio.run(
            publisher.run_communication_loop(mock_websocket, self.mock_zmq_socket)
        )

        self.assertEqual(mock_websocket.send.call_count, 2)
        self.assertEqual(mock_websocket.recv.call_count, 2)
        self.mock_zmq_socket.send_multipart.assert_called_once()

    def test_authentication_failure(self):
        mock_websocket = AsyncMock()
        auth_failure = msgpack.packb([{"T": "error", "msg": "auth failed"}])
        mock_websocket.recv.return_value = auth_failure

        asyncio.run(
            publisher.run_communication_loop(mock_websocket, self.mock_zmq_socket)
        )

        self.assertEqual(mock_websocket.send.call_count, 1)
        self.mock_zmq_socket.send_multipart.assert_not_called()


if __name__ == "__main__":
    unittest.main()
