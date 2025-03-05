import queue
from threading import Thread


class ThreadPool:
    def __init__(self, n_workers, queue_size=1) -> None:
        self.pool = [Thread(target=self.worker_main, daemon=True) for _ in range(n_workers)]
        self.queue = queue.Queue(maxsize=queue_size)

    def start(self):
        for thread in self.pool:
            thread.start()

    def try_run(self, fn, *args, **kwargs):
        try:
            self.queue.put_nowait((fn, args, kwargs))
            return True
        except queue.Full:
            return False

    def worker_main(self):
        while True:
            fn, args, kwargs = self.queue.get()
            fn(*args, **kwargs)
