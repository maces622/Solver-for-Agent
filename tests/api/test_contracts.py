#!/usr/bin/env python3
"""Validate the stage-1 GTO API JSON Schema contracts and golden messages."""

from __future__ import annotations

import json
import sys
import warnings
from pathlib import Path

try:
    from jsonschema import Draft202012Validator
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)
        from jsonschema.validators import RefResolver
except ImportError as exc:  # pragma: no cover - environment dependent
    raise SystemExit("Install the 'jsonschema' package to run contract tests") from exc


ROOT = Path(__file__).resolve().parents[2]
SCHEMA_DIR = ROOT / "api" / "schema"
GOLDEN_DIR = ROOT / "tests" / "api" / "golden"

CASES = [
    ("system.hello.request.schema.json", "hello.request.json"),
    ("system.hello.response.schema.json", "hello.response.json"),
    ("system.getCapabilities.request.schema.json", "capabilities.request.json"),
    ("system.getCapabilities.response.schema.json", "capabilities.response.json"),
    ("system.health.request.schema.json", "health.request.json"),
    ("system.health.response.schema.json", "health.response.json"),
    ("state.get.request.schema.json", "state_get.request.json"),
    ("state.get.response.schema.json", "state_get.response.json"),
    ("spot.validate.request.schema.json", "spot_validate.request.json"),
    ("spot.validate.response.schema.json", "spot_validate.response.json"),
    ("spot.set.request.schema.json", "spot_set.request.json"),
    ("spot.set.response.schema.json", "spot_set.response.json"),
    ("range.validate.request.schema.json", "range_validate.request.json"),
    ("range.validate.response.schema.json", "range_validate.response.json"),
    ("state.conflict.error.schema.json", "state_conflict.json"),
    ("solve.start.request.schema.json", "solve_start.request.json"),
    ("solve.start.response.schema.json", "solve_start.response.json"),
    ("solve.getStatus.request.schema.json", "solve_status.request.json"),
    ("solve.getStatus.response.schema.json", "solve_status.response.json"),
    ("solve.cancel.request.schema.json", "solve_cancel.request.json"),
    ("solve.cancel.response.schema.json", "solve_cancel.response.json"),
    ("event.state_changed.schema.json", "event_state_changed.json"),
    ("event.solve_queued.schema.json", "event_solve_queued.json"),
    ("event.solve_progress.schema.json", "event_solve_progress.json"),
    ("event.solve_completed.schema.json", "event_solve_completed.json"),
    ("event.solve_failed.schema.json", "event_solve_failed.json"),
    ("event.solve_cancelled.schema.json", "event_solve_cancelled.json"),
    ("solve.getSummary.request.schema.json", "solve_summary.request.json"),
    ("solve.getSummary.response.schema.json", "solve_summary.response.json"),
    ("result.getNodeStrategy.request.schema.json", "node_strategy.request.json"),
    ("result.getNodeStrategy.response.schema.json", "node_strategy.response.json"),
    ("result.getActionEv.request.schema.json", "action_ev.request.json"),
    ("result.getActionEv.response.schema.json", "action_ev.response.json"),
    ("result.getExploitability.request.schema.json", "exploitability.request.json"),
    ("result.getExploitability.response.schema.json", "exploitability.response.json"),
    ("artifact.getMetadata.request.schema.json", "artifact_metadata.request.json"),
    ("artifact.getMetadata.response.schema.json", "artifact_metadata.response.json"),
    ("ui.focusNode.request.schema.json", "ui_focus_node.request.json"),
    ("ui.focusNode.response.schema.json", "ui_focus_node.response.json"),
    ("ui.openResult.request.schema.json", "ui_open_result.request.json"),
    ("ui.openResult.response.schema.json", "ui_open_result.response.json"),
    ("event.selection_changed.schema.json", "event_selection_changed.json"),
    ("error.schema.json", "error_unauthorized.json"),
]


def load_json(path: Path) -> dict:
    with path.open("r", encoding="utf-8") as handle:
        return json.load(handle)


def make_validator(schema_name: str) -> Draft202012Validator:
    schema_path = SCHEMA_DIR / schema_name
    schema = load_json(schema_path)
    Draft202012Validator.check_schema(schema)
    with warnings.catch_warnings():
        warnings.simplefilter("ignore", DeprecationWarning)
        resolver = RefResolver(
            base_uri=schema_path.parent.as_uri() + "/",
            referrer=schema,
        )
    return Draft202012Validator(schema, resolver=resolver)


def main() -> int:
    failures: list[str] = []
    for schema_path in SCHEMA_DIR.glob("*.schema.json"):
        Draft202012Validator.check_schema(load_json(schema_path))

    for schema_name, golden_name in CASES:
        validator = make_validator(schema_name)
        instance = load_json(GOLDEN_DIR / golden_name)
        errors = sorted(validator.iter_errors(instance), key=lambda error: list(error.path))
        if errors:
            failures.append(f"{golden_name} does not match {schema_name}")
            for error in errors:
                location = "/".join(str(part) for part in error.path) or "<root>"
                failures.append(f"  {location}: {error.message}")
        else:
            print(f"ok: {golden_name} -> {schema_name}")

    if failures:
        print("\n".join(failures), file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
