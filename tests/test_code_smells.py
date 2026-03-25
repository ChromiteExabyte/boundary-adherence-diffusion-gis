"""Code smell tests for the boundary-adherence-diffusion-gis project.

These tests use Python's ``ast`` module to statically analyse all ``.py``
source files under ``src/`` and ``scripts/`` and flag common code smells
that reduce readability and maintainability.
"""
import ast
import tokenize
import io
from pathlib import Path
from typing import Generator

import pytest

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

PROJECT_ROOT = Path(__file__).resolve().parents[1]
SOURCE_DIRS = [PROJECT_ROOT / "src", PROJECT_ROOT / "scripts"]

# Thresholds
MAX_FUNCTION_LINES = 50          # body line span (end - start)
MAX_PARAMETERS = 5               # positional + keyword params (excl. self/cls)
MAX_NESTING_DEPTH = 4            # compound-statement nesting
MAX_FILE_LINES = 300             # total lines in a file
MAX_RETURNS_PER_FUNCTION = 4     # multiple return paths hint at complexity
ALLOWED_MAGIC_NUMBERS = {0, 1, -1, 2}  # numerics that are universally clear


def _source_files() -> list[Path]:
    """Return all .py files in the monitored source directories."""
    files: list[Path] = []
    for d in SOURCE_DIRS:
        if d.exists():
            files.extend(d.rglob("*.py"))
    return sorted(files)


def _parse(path: Path) -> tuple[ast.Module, list[str]]:
    source = path.read_text(encoding="utf-8")
    tree = ast.parse(source, filename=str(path))
    lines = source.splitlines()
    return tree, lines


def _functions(tree: ast.Module) -> Generator[ast.FunctionDef, None, None]:
    for node in ast.walk(tree):
        if isinstance(node, (ast.FunctionDef, ast.AsyncFunctionDef)):
            yield node


def _nesting_depth(node: ast.AST) -> int:
    """Return the maximum nesting depth of compound statements inside *node*."""
    compound = (ast.If, ast.For, ast.While, ast.With, ast.Try,
                ast.ExceptHandler, ast.AsyncFor, ast.AsyncWith)

    def _depth(n: ast.AST, current: int) -> int:
        max_depth = current
        for child in ast.iter_child_nodes(n):
            if isinstance(child, compound):
                max_depth = max(max_depth, _depth(child, current + 1))
            else:
                max_depth = max(max_depth, _depth(child, current))
        return max_depth

    return _depth(node, 0)


# ---------------------------------------------------------------------------
# Parametrised fixtures
# ---------------------------------------------------------------------------

SOURCE_FILES = _source_files()


@pytest.fixture(params=SOURCE_FILES, ids=[str(p.relative_to(PROJECT_ROOT)) for p in SOURCE_FILES])
def source_file(request) -> Path:
    return request.param


# ---------------------------------------------------------------------------
# File-level checks
# ---------------------------------------------------------------------------

def test_file_not_too_long(source_file):
    """Files should not exceed MAX_FILE_LINES lines."""
    lines = source_file.read_text(encoding="utf-8").splitlines()
    assert len(lines) <= MAX_FILE_LINES, (
        f"{source_file.relative_to(PROJECT_ROOT)} has {len(lines)} lines "
        f"(limit {MAX_FILE_LINES}). Consider splitting it into smaller modules."
    )


def test_no_star_imports(source_file):
    """Star imports (``from x import *``) pollute the namespace."""
    tree, _ = _parse(source_file)
    violations = [
        node
        for node in ast.walk(tree)
        if isinstance(node, ast.ImportFrom)
        and any(alias.name == "*" for alias in node.names)
    ]
    assert not violations, (
        f"{source_file.relative_to(PROJECT_ROOT)}: star import(s) found at "
        f"lines {[n.lineno for n in violations]}."
    )


def test_no_bare_except(source_file):
    """Bare ``except:`` clauses swallow all exceptions including KeyboardInterrupt."""
    tree, _ = _parse(source_file)
    violations = [
        node
        for node in ast.walk(tree)
        if isinstance(node, ast.ExceptHandler) and node.type is None
    ]
    assert not violations, (
        f"{source_file.relative_to(PROJECT_ROOT)}: bare except clause(s) at "
        f"lines {[n.lineno for n in violations]}. Use 'except Exception:' or a specific type."
    )


def test_no_todo_fixme_comments(source_file):
    """TODO/FIXME comments signal unfinished work that should not reach the main branch."""
    source = source_file.read_text(encoding="utf-8")
    tokens = tokenize.generate_tokens(io.StringIO(source).readline)
    violations = []
    for tok_type, tok_string, tok_start, *_ in tokens:
        if tok_type == tokenize.COMMENT:
            upper = tok_string.upper()
            if "TODO" in upper or "FIXME" in upper:
                violations.append(tok_start[0])
    assert not violations, (
        f"{source_file.relative_to(PROJECT_ROOT)}: TODO/FIXME comment(s) at "
        f"lines {violations}."
    )


# ---------------------------------------------------------------------------
# Function-level checks
# ---------------------------------------------------------------------------

def test_functions_not_too_long(source_file):
    """Functions with many lines are hard to understand and test."""
    tree, _ = _parse(source_file)
    violations = []
    for fn in _functions(tree):
        span = fn.end_lineno - fn.lineno
        if span > MAX_FUNCTION_LINES:
            violations.append((fn.name, fn.lineno, span))
    assert not violations, (
        f"{source_file.relative_to(PROJECT_ROOT)}: function(s) exceed "
        f"{MAX_FUNCTION_LINES} lines: "
        + ", ".join(f"{name}() at line {ln} ({sp} lines)" for name, ln, sp in violations)
    )


def test_functions_not_too_many_parameters(source_file):
    """Functions with many parameters are hard to call and often signal missing abstractions."""
    tree, _ = _parse(source_file)
    violations = []
    for fn in _functions(tree):
        args = fn.args
        # exclude 'self' and 'cls' for methods
        params = [
            a for a in args.args
            if a.arg not in ("self", "cls")
        ]
        # include *args and **kwargs in the count
        total = len(params) + len(args.posonlyargs) + len(args.kwonlyargs)
        if total > MAX_PARAMETERS:
            violations.append((fn.name, fn.lineno, total))
    assert not violations, (
        f"{source_file.relative_to(PROJECT_ROOT)}: function(s) have more than "
        f"{MAX_PARAMETERS} parameters: "
        + ", ".join(f"{name}() at line {ln} ({n} params)" for name, ln, n in violations)
    )


def test_functions_not_too_deeply_nested(source_file):
    """Deeply nested code is hard to follow; extract inner logic into helper functions."""
    tree, _ = _parse(source_file)
    violations = []
    for fn in _functions(tree):
        depth = _nesting_depth(fn)
        if depth > MAX_NESTING_DEPTH:
            violations.append((fn.name, fn.lineno, depth))
    assert not violations, (
        f"{source_file.relative_to(PROJECT_ROOT)}: function(s) exceed nesting depth "
        f"{MAX_NESTING_DEPTH}: "
        + ", ".join(f"{name}() at line {ln} (depth {d})" for name, ln, d in violations)
    )


def test_functions_not_too_many_return_statements(source_file):
    """Many return paths increase cyclomatic complexity and make control flow hard to trace."""
    tree, _ = _parse(source_file)
    violations = []
    for fn in _functions(tree):
        returns = sum(1 for n in ast.walk(fn) if isinstance(n, ast.Return))
        if returns > MAX_RETURNS_PER_FUNCTION:
            violations.append((fn.name, fn.lineno, returns))
    assert not violations, (
        f"{source_file.relative_to(PROJECT_ROOT)}: function(s) have more than "
        f"{MAX_RETURNS_PER_FUNCTION} return statements: "
        + ", ".join(f"{name}() at line {ln} ({r} returns)" for name, ln, r in violations)
    )


def test_no_magic_numbers_in_functions(source_file):
    """Unexplained numeric literals should be named constants for clarity."""
    tree, _ = _parse(source_file)
    violations = []
    for fn in _functions(tree):
        for node in ast.walk(fn):
            if isinstance(node, ast.Constant) and isinstance(node.value, (int, float)):
                if node.value not in ALLOWED_MAGIC_NUMBERS:
                    violations.append((fn.name, node.lineno, node.value))
    assert not violations, (
        f"{source_file.relative_to(PROJECT_ROOT)}: magic number(s) inside function bodies "
        f"(consider named constants): "
        + ", ".join(
            f"{name}() line {ln}: {val}"
            for name, ln, val in violations
        )
    )
