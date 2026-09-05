import importlib.util
from pathlib import Path
import unittest

SPEC = importlib.util.spec_from_file_location(
    "audio_gate", Path(__file__).resolve().parents[1] / "check_node_audio_performance.py")
gate = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(gate)


def record(ms=5000, frames=220500, blocks=500, **changes):
    row = dict(uptime_ms=ms, frames=frames, blocks=blocks, max_render_us=4000,
               max_load_permille=800, deadline_misses=0,
               producer_starvations=0, write_errors=0)
    row.update(changes)
    return row


class AudioPerformanceTests(unittest.TestCase):
    def test_five_minutes_with_twenty_percent_headroom_passes(self):
        rows = [record(), record(305000, 13450500, 30500)]
        self.assertTrue(gate.evaluate(rows)["passed"])

    def test_one_microsecond_over_budget_is_not_hidden(self):
        rows = [record(), record(305000, 13450500, 30500, max_load_permille=801)]
        self.assertFalse(gate.evaluate(rows)["passed"])

    def test_cumulative_errors_cannot_be_hidden_by_trimming_capture(self):
        for field in ("deadline_misses", "producer_starvations", "write_errors"):
            rows = [record(**{field: 1}), record(305000, 13450500, 30500, **{field: 1})]
            self.assertFalse(gate.evaluate(rows)["passed"])

    def test_short_or_stalled_capture_fails(self):
        self.assertFalse(gate.evaluate([record(), record(10000, 441000, 1000)])["passed"])
        self.assertFalse(gate.evaluate([record(), record(305000)])["passed"])

    def test_serial_prefix_and_unrelated_logs_are_accepted(self):
        lines = ["booting"]
        for row in (record(), record(305000, 13450500, 30500)):
            lines.append("I (5000) AUDIO_PERF: " + " ".join(f"{k}={v}" for k, v in row.items()))
        self.assertTrue(gate.evaluate(gate.read_metrics("\n".join(lines)))["passed"])

    def test_missing_duplicate_and_reset_records_are_rejected(self):
        for text in ("", "AUDIO_PERF uptime_ms=5", "AUDIO_PERF uptime_ms=5 uptime_ms=5"):
            with self.assertRaises(ValueError):
                gate.read_metrics(text)
        text = "\n".join("AUDIO_PERF " + " ".join(f"{k}={v}" for k, v in row.items())
                         for row in (record(), record(ms=1)))
        with self.assertRaises(ValueError):
            gate.read_metrics(text)


if __name__ == "__main__":
    unittest.main()
