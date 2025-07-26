import asyncio
import os
import sys
import unittest
from unittest.mock import AsyncMock, MagicMock, Mock, patch

import msgpack

import publisher


class TestPublisher(unittest.TestCase):

    def setUp(self):
        self.mock_zmq_socket = AsyncMock()
        self.original_api_key = os.environ.get("APCA_API_KEY_ID")
        self.original_api_secret = os.environ.get("APCA_API_SECRET_KEY")

        os.environ["APCA_API_KEY_ID"] = "dummy_api_key"  # nosec
        os.environ["APCA_API_SECRET_KEY"] = "dummy_api_secret"  # nosec

    def tearDown(self):
        if self.original_api_key is not None:
            os.environ["APCA_API_KEY_ID"] = self.original_api_key
        else:
            os.environ.pop("APCA_API_KEY_ID", None)

        if self.original_api_secret is not None:
            os.environ["APCA_API_SECRET_KEY"] = self.original_api_secret
        else:
            os.environ.pop("APCA_API_SECRET_KEY", None)

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
            unpacked_data[3], int(sample_quote["bp"] * publisher.SCALING_FACTOR)
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
        self.assertEqual(
            unpacked_data[3], int(sample_trade["p"] * publisher.SCALING_FACTOR)
        )
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

    def test_subscription_failure(self):
        """Test subscription failure after successful authentication."""
        mock_websocket = AsyncMock()
        auth_success = msgpack.packb([{"T": "success", "msg": "authenticated"}])
        sub_failure = msgpack.packb([{"T": "error", "msg": "subscription failed"}])
        mock_websocket.recv.side_effect = [auth_success, sub_failure]

        asyncio.run(
            publisher.run_communication_loop(mock_websocket, self.mock_zmq_socket)
        )

        self.assertEqual(mock_websocket.send.call_count, 2)
        self.assertEqual(mock_websocket.recv.call_count, 2)
        self.mock_zmq_socket.send_multipart.assert_not_called()

    @patch("time.time_ns", return_value=5555555555)
    def test_handle_default_timestamp_conversion(self, _):
        """Test handling of regular timestamp values (non-msgpack.Timestamp)."""
        sample_quote = {
            "T": "q",
            "S": "MSFT",
            "t": 1678886400000000000,
            "bp": 300.00,
            "bs": 150,
            "ap": 300.05,
            "as": 250,
        }
        message = msgpack.packb([sample_quote])

        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))

        self.mock_zmq_socket.send_multipart.assert_called_once()
        unpacked_data = publisher.MARKET_EVENT.unpack(
            self.mock_zmq_socket.send_multipart.call_args[0][0][1]
        )

        self.assertEqual(unpacked_data[2], 1678886400000000000)

    @patch("time.time_ns", return_value=5555555555)
    def test_handle_timestamp_conversion_with_float(self, _):
        """Test timestamp handling with float values."""
        sample_quote = {
            "T": "q",
            "S": "MSFT",
            "t": 1678886400.123456,
            "bp": 300.00,
            "bs": 150,
            "ap": 300.05,
            "as": 250,
        }
        message = msgpack.packb([sample_quote])

        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))

        self.mock_zmq_socket.send_multipart.assert_called_once()
        unpacked_data = publisher.MARKET_EVENT.unpack(
            self.mock_zmq_socket.send_multipart.call_args[0][0][1]
        )

        self.assertEqual(unpacked_data[2], 1678886400)

    def test_handle_empty_message_list(self):
        """Test handling of empty message list."""
        message = msgpack.packb([])
        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))
        self.mock_zmq_socket.send_multipart.assert_not_called()

    def test_handle_non_list_message(self):
        """Test handling of non-list message."""
        message = msgpack.packb({"T": "q", "S": "AAPL"})
        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))
        self.mock_zmq_socket.send_multipart.assert_not_called()

    def test_handle_missing_message_type(self):
        """Test handling of message with missing type field."""
        sample_message = {"S": "AAPL", "t": 123456789}
        message = msgpack.packb([sample_message])
        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))
        self.mock_zmq_socket.send_multipart.assert_not_called()

    def test_handle_missing_symbol(self):
        """Test handling of message with missing symbol field."""
        sample_message = {"T": "q", "t": 123456789}
        message = msgpack.packb([sample_message])
        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))
        self.mock_zmq_socket.send_multipart.assert_not_called()

    def test_handle_unknown_message_type(self):
        """Test handling of unknown message type."""
        sample_message = {"T": "unknown", "S": "AAPL", "t": 123456789}
        message = msgpack.packb([sample_message])
        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))
        self.mock_zmq_socket.send_multipart.assert_not_called()

    @patch("time.time_ns", return_value=7777777777)
    def test_handle_quote_with_missing_fields(self, _):
        """Test handling of quote with missing price/size fields."""
        sample_quote = {
            "T": "q",
            "S": "TEST",
            "t": 1678886400000000000,
            # Missing bp, bs, ap, as fields - should default to 0
        }
        message = msgpack.packb([sample_quote])

        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))

        self.mock_zmq_socket.send_multipart.assert_called_once()
        unpacked_data = publisher.MARKET_EVENT.unpack(
            self.mock_zmq_socket.send_multipart.call_args[0][0][1]
        )

        self.assertEqual(unpacked_data[3], 0)  # bid price
        self.assertEqual(unpacked_data[4], 0)  # bid size
        self.assertEqual(unpacked_data[5], 0)  # ask price
        self.assertEqual(unpacked_data[6], 0)  # ask size

    @patch("time.time_ns", return_value=8888888888)
    def test_handle_trade_with_missing_fields(self, _):
        """Test handling of trade with missing price/size fields."""
        sample_trade = {
            "T": "t",
            "S": "TEST",
            "t": 1678886400000000000,
            # Missing p, s fields - should default to 0
        }
        message = msgpack.packb([sample_trade])

        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))

        self.mock_zmq_socket.send_multipart.assert_called_once()
        unpacked_data = publisher.MARKET_EVENT.unpack(
            self.mock_zmq_socket.send_multipart.call_args[0][0][1]
        )

        self.assertEqual(unpacked_data[3], 0)  # trade price
        self.assertEqual(unpacked_data[4], 0)  # trade size
        self.assertEqual(unpacked_data[5], 0)  # unused
        self.assertEqual(unpacked_data[6], 0)  # unused

    def test_handle_msgpack_unpack_exception(self):
        """Test handling of msgpack unpacking exceptions."""
        invalid_message = b"\x80\x81\x82invalid"

        asyncio.run(publisher.handle_market_data(invalid_message, self.mock_zmq_socket))
        self.mock_zmq_socket.send_multipart.assert_not_called()

    def test_handle_index_error(self):
        """Test handling of IndexError during message processing."""
        with patch("msgpack.unpackb") as mock_unpack:
            mock_unpack.side_effect = IndexError("Index out of range")

            message = msgpack.packb([{"T": "q", "S": "AAPL"}])
            asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))
            self.mock_zmq_socket.send_multipart.assert_not_called()

    def test_handle_attribute_error(self):
        """Test handling of AttributeError during message processing."""
        with patch("msgpack.unpackb") as mock_unpack:
            mock_unpack.side_effect = AttributeError("Attribute not found")

            message = msgpack.packb([{"T": "q", "S": "AAPL"}])
            asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))
            self.mock_zmq_socket.send_multipart.assert_not_called()

    def test_handle_long_symbol_truncation(self):
        """Test that long symbol names are properly truncated."""
        sample_quote = {
            "T": "q",
            "S": "VERYLONGSYMBOL",
            "t": 1678886400000000000,
            "bp": 100.0,
            "bs": 50,
            "ap": 100.5,
            "as": 75,
        }
        message = msgpack.packb([sample_quote])

        asyncio.run(publisher.handle_market_data(message, self.mock_zmq_socket))

        self.mock_zmq_socket.send_multipart.assert_called_once()
        call_args = self.mock_zmq_socket.send_multipart.call_args[0][0]
        sent_topic = call_args[0]
        self.assertEqual(sent_topic, b"VERYLONGSYMBOL")

        unpacked_data = publisher.MARKET_EVENT.unpack(call_args[1])
        self.assertEqual(unpacked_data[1], b"VERYLON")

    def test_get_zmq_address(self):
        """Test that the correct ZMQ address is returned for each platform."""
        original_platform = sys.platform

        try:
            sys.platform = "win32"
            self.assertEqual(publisher.get_zmq_address(), "tcp://127.0.0.1:5555")

            sys.platform = "linux"
            address = publisher.get_zmq_address()
            self.assertTrue(address.startswith("ipc://"))
            self.assertTrue(address.endswith("market_data.sock"))

            sys.platform = "darwin"
            address = publisher.get_zmq_address()
            self.assertTrue(address.startswith("ipc://"))
            self.assertTrue(address.endswith("market_data.sock"))
        finally:
            sys.platform = original_platform

    @patch("tempfile.gettempdir")
    def test_get_zmq_address_unix_path_construction(self, mock_gettempdir):
        """Test that Unix paths are constructed correctly."""
        original_platform = sys.platform
        try:
            sys.platform = "linux"
            mock_gettempdir.return_value = "/tmp"  # nosec

            address = publisher.get_zmq_address()
            expected = "ipc:///tmp/market_data.sock"
            self.assertEqual(address, expected)
        finally:
            sys.platform = original_platform

    @patch("os.sched_setaffinity", create=True)
    def test_cpu_affinity_linux(self, mock_setaffinity):
        """Test CPU affinity setting on Linux."""
        original_platform = sys.platform
        try:
            sys.platform = "linux"
            publisher.set_cpu_affinity(2)
            mock_setaffinity.assert_called_once_with(0, {2})
        finally:
            sys.platform = original_platform

    @patch("os.sched_setaffinity", create=True)
    def test_cpu_affinity_linux_exception(self, mock_setaffinity):
        """Test CPU affinity setting on Linux with exception."""
        original_platform = sys.platform
        try:
            sys.platform = "linux"
            mock_setaffinity.side_effect = OSError("Permission denied")

            publisher.set_cpu_affinity(1)
            mock_setaffinity.assert_called_once_with(0, {1})
        finally:
            sys.platform = original_platform

    def test_cpu_affinity_windows(self):
        """Test CPU affinity setting on Windows."""
        original_platform = sys.platform
        try:
            sys.platform = "win32"
            with patch.dict(
                "sys.modules", {"win32api": MagicMock(), "win32process": MagicMock()}
            ):
                import win32api
                import win32process

                mock_handle = MagicMock()
                win32api.GetCurrentProcess.return_value = mock_handle

                publisher.set_cpu_affinity(1)
                win32api.GetCurrentProcess.assert_called_once()
                win32process.SetProcessAffinityMask.assert_called_once_with(
                    mock_handle, 2
                )
        finally:
            sys.platform = original_platform

    def test_cpu_affinity_windows_import_error(self):
        """Test CPU affinity setting on Windows with import error."""
        original_platform = sys.platform
        try:
            sys.platform = "win32"
            publisher.set_cpu_affinity(1)
        finally:
            sys.platform = original_platform

    def test_cpu_affinity_macos(self):
        """Test CPU affinity setting on macOS (should be no-op)."""
        original_platform = sys.platform
        try:
            sys.platform = "darwin"
            publisher.set_cpu_affinity(0)
        finally:
            sys.platform = original_platform

    def test_cpu_affinity_unknown_platform(self):
        """Test CPU affinity setting on unknown platform."""
        original_platform = sys.platform
        try:
            sys.platform = "unknown_os"
            publisher.set_cpu_affinity(0)
        finally:
            sys.platform = original_platform

    def test_run_loop_missing_api_credentials(self):
        """Test run_communication_loop with missing API credentials."""
        os.environ.pop("APCA_API_KEY_ID", None)
        os.environ.pop("APCA_API_SECRET_KEY", None)
        mock_websocket = AsyncMock()

        asyncio.run(
            publisher.run_communication_loop(mock_websocket, self.mock_zmq_socket)
        )

        mock_websocket.send.assert_not_called()

    def test_run_loop_partial_api_credentials(self):
        """Test run_communication_loop with only one API credential."""
        os.environ["APCA_API_KEY_ID"] = "test_key"
        os.environ.pop("APCA_API_SECRET_KEY", None)
        mock_websocket = AsyncMock()

        asyncio.run(
            publisher.run_communication_loop(mock_websocket, self.mock_zmq_socket)
        )

        mock_websocket.send.assert_not_called()

    def test_run_loop_websocket_exception(self):
        """Test run_communication_loop with websocket exception."""
        mock_websocket = AsyncMock()
        mock_websocket.send.side_effect = Exception("Connection error")

        asyncio.run(
            publisher.run_communication_loop(mock_websocket, self.mock_zmq_socket)
        )

        mock_websocket.send.assert_called_once()

    def test_run_loop_auth_response_not_list(self):
        """Test authentication with non-list response."""
        mock_websocket = AsyncMock()
        auth_response = msgpack.packb({"T": "success"})
        mock_websocket.recv.return_value = auth_response

        asyncio.run(
            publisher.run_communication_loop(mock_websocket, self.mock_zmq_socket)
        )

        self.assertEqual(mock_websocket.send.call_count, 1)
        self.mock_zmq_socket.send_multipart.assert_not_called()

    def test_run_loop_auth_response_empty_list(self):
        """Test authentication with empty list response."""
        mock_websocket = AsyncMock()
        auth_response = msgpack.packb([])
        mock_websocket.recv.return_value = auth_response

        asyncio.run(
            publisher.run_communication_loop(mock_websocket, self.mock_zmq_socket)
        )

        self.assertEqual(mock_websocket.send.call_count, 1)
        self.mock_zmq_socket.send_multipart.assert_not_called()

    def test_run_loop_sub_response_not_list(self):
        """Test subscription with non-list response."""
        mock_websocket = AsyncMock()
        auth_success = msgpack.packb([{"T": "success", "msg": "authenticated"}])
        sub_response = msgpack.packb({"T": "success"})
        mock_websocket.recv.side_effect = [auth_success, sub_response]

        asyncio.run(
            publisher.run_communication_loop(mock_websocket, self.mock_zmq_socket)
        )

        self.assertEqual(mock_websocket.send.call_count, 2)
        self.mock_zmq_socket.send_multipart.assert_not_called()

    @patch("publisher.set_cpu_affinity")
    @patch("websockets.connect")
    @patch("zmq.asyncio.Context")
    @patch("publisher.get_zmq_address")
    def test_main_function_success(
        self, mock_get_address, mock_zmq_context, mock_websockets, mock_cpu_affinity
    ):
        """Test the main function with successful execution."""
        mock_get_address.return_value = "ipc:///tmp/test"

        mock_context = Mock()
        mock_socket = Mock()
        mock_context.socket.return_value = mock_socket
        mock_zmq_context.return_value = mock_context

        mock_websocket = AsyncMock()
        mock_websockets.return_value.__aenter__.return_value = mock_websocket

        with patch("publisher.run_communication_loop") as mock_run_loop:
            mock_run_loop.return_value = None

            asyncio.run(publisher.main())

            mock_get_address.assert_called_once()
            mock_context.socket.assert_called_once()
            mock_socket.bind.assert_called_once_with("ipc:///tmp/test")
            mock_run_loop.assert_called_once()
            mock_socket.close.assert_called_once()
            mock_context.term.assert_called_once()

    @patch("publisher.set_cpu_affinity")
    @patch("websockets.connect")
    @patch("zmq.asyncio.Context")
    @patch("publisher.get_zmq_address")
    def test_main_function_with_custom_address(
        self, mock_get_address, mock_zmq_context, mock_websockets, mock_cpu_affinity
    ):
        """Test the main function with custom ZMQ address."""
        custom_address = "tcp://localhost:6666"

        mock_context = Mock()
        mock_socket = Mock()
        mock_context.socket.return_value = mock_socket
        mock_zmq_context.return_value = mock_context

        mock_websocket = AsyncMock()
        mock_websockets.return_value.__aenter__.return_value = mock_websocket

        with patch("publisher.run_communication_loop") as mock_run_loop:
            mock_run_loop.return_value = None

            asyncio.run(publisher.main(custom_address))

            mock_get_address.assert_not_called()
            mock_socket.bind.assert_called_once_with(custom_address)

    @patch("publisher.set_cpu_affinity")
    @patch("websockets.connect")
    @patch("zmq.asyncio.Context")
    @patch("publisher.get_zmq_address")
    def test_main_function_keyboard_interrupt(
        self, mock_get_address, mock_zmq_context, mock_websockets, mock_cpu_affinity
    ):
        """Test the main function with KeyboardInterrupt."""
        mock_get_address.return_value = "ipc:///tmp/test"

        mock_context = Mock()
        mock_socket = Mock()
        mock_context.socket.return_value = mock_socket
        mock_zmq_context.return_value = mock_context
        mock_websockets.side_effect = KeyboardInterrupt("User interrupt")

        asyncio.run(publisher.main())

        mock_socket.close.assert_called_once()
        mock_context.term.assert_called_once()

    @patch("publisher.set_cpu_affinity")
    @patch("websockets.connect")
    @patch("zmq.asyncio.Context")
    @patch("publisher.get_zmq_address")
    def test_main_function_general_exception(
        self, mock_get_address, mock_zmq_context, mock_websockets, mock_cpu_affinity
    ):
        """Test the main function with general exception."""
        mock_get_address.return_value = "ipc:///tmp/test"

        mock_context = Mock()
        mock_socket = Mock()
        mock_context.socket.return_value = mock_socket
        mock_zmq_context.return_value = mock_context

        mock_websockets.side_effect = Exception("Connection failed")
        asyncio.run(publisher.main())

        mock_socket.close.assert_called_once()
        mock_context.term.assert_called_once()

    @patch("publisher.set_cpu_affinity")
    @patch("websockets.connect")
    @patch("zmq.asyncio.Context")
    @patch("zmq.ZMQError")
    def test_main_function_zmq_bind_error(
        self, mock_zmq_error, mock_zmq_context, mock_websockets, mock_cpu_affinity
    ):
        """Test the main function with ZMQ bind error."""
        mock_context = Mock()
        mock_socket = Mock()
        mock_context.socket.return_value = mock_socket
        mock_zmq_context.return_value = mock_context

        import zmq

        mock_socket.bind.side_effect = zmq.ZMQError(1)

        with patch("publisher.get_zmq_address", return_value="ipc:///tmp/test"):
            asyncio.run(publisher.main())

        mock_socket.close.assert_called_once()
        mock_context.term.assert_called_once()


if __name__ == "__main__":
    unittest.main()
