#!/usr/bin/env python3
"""Assign weighted tasks to a fixed number of workers.

This module provides a function, `balance` that greedily assigns the
lowest-weighted task to the worker with the least amount of total
work.

This module can be executed as a script.  In that case, the number
of workers is provided as a command line argument, and
lines of whitespace-separated (cost, task) pairs are read from
standard input.  The resulting assignment of tasks to workers is
printed to standard output, one line per worker, tasks separated by
spaces.
"""

import heapq


def balance(tasks, num_workers):
    tasks_by_rank = [[] for _ in range(num_workers)]
    # A heap item is (burden, rank).  The burden is the total
    # cost of all tasks assigned to that worker.  The rank is the
    # index into `tasks_by_rank` where the worker's tasks are stored.
    heap = [(0, rank) for rank in range(num_workers)]
    heapq.heapify(heap)

    for cost, task in sorted(tasks):
        burden, rank = heap[0]
        tasks = tasks_by_rank[rank]
        burden += cost
        tasks.append(task)
        heapq.heapreplace(heap, (burden, rank))

    return tasks_by_rank


if __name__ == '__main__':
    import sys

    num_workers = int(sys.argv[1])
    tasks = []
    for line in sys.stdin:
        cost, task = line.split()
        tasks.append((float(cost), task))

    for assigned_tasks in balance(tasks, num_workers):
        print(' '.join(assigned_tasks))
