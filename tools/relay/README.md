# Relay 测试 ESD

esd_mock.py 在回环地址提供设备发现和中继查询，可以设置业务错误、坏 JSON、缺字段、空列表及延迟。凭据均为合成测试值。运行 `python3 -m unittest discover -s tools/relay -p 'test_*.py'` 验证夹具；运行 [Relay 集成测试](../../tests/test_relay_live.py) 验证实际服务与 TCP 中继、HLS 和转码的交互。
