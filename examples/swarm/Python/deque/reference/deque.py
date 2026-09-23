# Reference semantic model for Python collections.deque (Int elements).
#
# CPython implements deque in C (Modules/_collectionsmodule.c) as a doubly
# linked list of fixed-size blocks. This file is NOT a copy of that code: it is
# a small pure-Python *semantic model* of the public API subset targeted by the
# Moss translation, written against the documented behaviour of
# https://docs.python.org/3/library/collections.html#collections.deque
# and checked against CPython's deque on the test vectors used in main.moss.
#
# Representation here (ring buffer over a list) mirrors the Moss translation,
# not CPython's block list; only observable behaviour is meant to match.


class deque:
    def __init__(self, iterable=(), maxlen=None):
        if maxlen is not None and maxlen < 0:
            raise ValueError("maxlen must be non-negative")
        self.maxlen = maxlen
        cap = 4 if maxlen is None else max(maxlen, 1)
        self._buf = [0] * cap
        self._head = 0
        self._size = 0
        self.extend(iterable)          # bounded: keeps only the rightmost maxlen

    def __len__(self):
        return self._size

    def _cap(self):
        return len(self._buf)

    def _grow(self):
        cap = self._cap()
        fresh = [0] * (cap * 2)
        for i in range(self._size):
            fresh[i] = self._buf[(self._head + i) % cap]
        self._buf, self._head = fresh, 0

    def append(self, x):
        if self.maxlen == 0:
            return                     # deque(maxlen=0) silently discards
        if self._size == self._cap():
            if self.maxlen is not None:
                # full bounded deque: discard from the LEFT
                self._buf[self._head] = x
                self._head = (self._head + 1) % self._cap()
                return
            self._grow()
        self._buf[(self._head + self._size) % self._cap()] = x
        self._size += 1

    def appendleft(self, x):
        if self.maxlen == 0:
            return
        if self._size == self._cap():
            if self.maxlen is not None:
                # full bounded deque: discard from the RIGHT
                self._head = (self._head - 1) % self._cap()
                self._buf[self._head] = x
                return
            self._grow()
        self._head = (self._head - 1) % self._cap()
        self._buf[self._head] = x
        self._size += 1

    def pop(self):
        if self._size == 0:
            raise IndexError("pop from an empty deque")
        self._size -= 1
        return self._buf[(self._head + self._size) % self._cap()]

    def popleft(self):
        if self._size == 0:
            raise IndexError("pop from an empty deque")
        x = self._buf[self._head]
        self._head = (self._head + 1) % self._cap()
        self._size -= 1
        return x

    def extend(self, iterable):
        for x in iterable:
            self.append(x)

    def extendleft(self, iterable):
        # each element is appendleft-ed in turn, so the result is reversed
        for x in iterable:
            self.appendleft(x)

    def rotate(self, n=1):
        # rotate right by n (left when n is negative); n is reduced mod len
        if self._size <= 1:
            return
        k = n % self._size             # Python floor modulo
        for _ in range(k):
            self.appendleft(self.pop())

    def __getitem__(self, i):
        if i < 0:
            i += self._size
        if not 0 <= i < self._size:
            raise IndexError("deque index out of range")
        return self._buf[(self._head + i) % self._cap()]

    def clear(self):
        self._head = 0
        self._size = 0

    def count(self, x):
        return sum(1 for i in range(self._size) if self[i] == x)

    def reverse(self):
        lo, hi = 0, self._size - 1
        while lo < hi:
            a = (self._head + lo) % self._cap()
            b = (self._head + hi) % self._cap()
            self._buf[a], self._buf[b] = self._buf[b], self._buf[a]
            lo, hi = lo + 1, hi - 1

    def __iter__(self):
        return (self[i] for i in range(self._size))


if __name__ == "__main__":
    import collections
    for model in (deque, collections.deque):
        d = model([1, 2, 3])
        d.appendleft(0)
        d.append(4)
        d.rotate(2)
        b = model(maxlen=3)
        b.extend([1, 2, 3, 4, 5])
        print(list(d), d[0], d[-1], b[0], b.pop(), b.count(3))
