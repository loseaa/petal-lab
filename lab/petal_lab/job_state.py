"""Finite-state machine for background task (job) lifecycle.

The server owns the lifecycle. petal (C++) is treated as a black box: the web
layer never tells it about these states, it only launches the process and
observes the exit code. Every transition must pass through ``transition`` so
illegal moves (for example ``done -> running``) are rejected loudly instead of
silently corrupting a task's state.
"""
from __future__ import annotations

# --- states -------------------------------------------------------------
PENDING = "pending"      # 已排队，等待并发槽
RUNNING = "running"      # 子进程已启动
DONE = "done"            # 成功退出且结果已入库
FAILED = "failed"        # 非零退出 / 异常
CANCELLED = "cancelled"  # 用户取消

TERMINAL = frozenset({DONE, FAILED, CANCELLED})
ACTIVE = frozenset({PENDING, RUNNING})

STATES = (PENDING, RUNNING, DONE, FAILED, CANCELLED)

# from_state -> allowed to_state
_TRANSITIONS: dict[str, frozenset[str]] = {
    PENDING: frozenset({RUNNING, CANCELLED}),
    RUNNING: frozenset({DONE, FAILED, CANCELLED}),
    DONE: frozenset(),
    FAILED: frozenset(),
    CANCELLED: frozenset(),
}

# 中文标签 + 前端徽标语义（与 web/src 中 status-badge class 对应）
LABELS: dict[str, str] = {
    PENDING: "排队中",
    RUNNING: "运行中",
    DONE: "完成",
    FAILED: "失败",
    CANCELLED: "已取消",
}

# 状态机可视化顺序（前端图例用）
FLOW = [PENDING, RUNNING, DONE, FAILED, CANCELLED]


class InvalidTransition(ValueError):
    def __init__(self, frm: str, to: str):
        super().__init__(f"非法的任务状态转移: {frm!r} -> {to!r}")
        self.frm = frm
        self.to = to


def is_terminal(state: str) -> bool:
    return state in TERMINAL


def can_transition(frm: str, to: str) -> bool:
    allowed = _TRANSITIONS.get(frm)
    if allowed is None:
        return False
    return to in allowed


def transition(frm: str, to: str) -> str:
    """Return ``to`` if the transition is legal; raise ``InvalidTransition`` otherwise."""
    if frm == to:
        return to
    if not can_transition(frm, to):
        raise InvalidTransition(frm, to)
    return to
