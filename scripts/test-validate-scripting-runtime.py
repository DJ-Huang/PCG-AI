#!/usr/bin/env python3
"""Offline tests for report validation; NOT substitutes for native probes."""
import argparse
import copy
import importlib.util
from pathlib import Path
import unittest

spec = importlib.util.spec_from_file_location("runtime_validation", Path(__file__).with_name("validate-scripting-runtime.py"))
runner = importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class MeasurementTests(unittest.TestCase):
    def setUp(self):
        self.commit = "a" * 40
        self.data = {
            "engine": "QuickJS-NG", "commit": self.commit, "samples": 5, "warmup_samples": 20,
            "metrics": {name: {"min_us": 1, "p50_us": 2, "p95_us": 4, "max_us": 5, "mean_us": 3}
                        for name in runner.METRICS},
        }

    def test_complete_report(self):
        runner.validate_measurement(self.data, 5, self.commit)

    def test_wrong_pin(self):
        with self.assertRaises(ValueError):
            runner.validate_measurement(self.data, 5, "b" * 40)

    def test_wrong_sample_count(self):
        with self.assertRaises(ValueError):
            runner.validate_measurement(self.data, 6, self.commit)

    def test_missing_metric(self):
        del self.data["metrics"][runner.METRICS[0]]
        with self.assertRaises(ValueError):
            runner.validate_measurement(self.data, 5, self.commit)

    def test_invalid_numbers(self):
        for value in (float("nan"), float("inf"), -1, True, "2", None):
            with self.subTest(value=value):
                data = copy.deepcopy(self.data)
                data["metrics"][runner.METRICS[0]]["p95_us"] = value
                with self.assertRaises(ValueError):
                    runner.validate_measurement(data, 5, self.commit)

    def test_invalid_distribution(self):
        self.data["metrics"][runner.METRICS[0]]["p50_us"] = 6
        with self.assertRaises(ValueError):
            runner.validate_measurement(self.data, 5, self.commit)

    def test_invalid_root(self):
        for data in (None, [], "not-json-object"):
            with self.assertRaises(ValueError):
                runner.validate_measurement(data, 5, self.commit)

    def test_invalid_metrics(self):
        self.data["metrics"] = None
        with self.assertRaises(ValueError):
            runner.validate_measurement(self.data, 5, self.commit)

    def test_sample_bounds(self):
        self.assertEqual(runner.sample_count("5"), 5)
        self.assertEqual(runner.sample_count("10000"), 10000)
        for value in ("0", "4", "10001"):
            with self.assertRaises(argparse.ArgumentTypeError):
                runner.sample_count(value)


if __name__ == "__main__":
    unittest.main()
