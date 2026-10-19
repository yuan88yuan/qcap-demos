#!/usr/bin/env python3
"""Compile/run the production PCM queue helper with fake writes in the builder."""
from pathlib import Path
import subprocess
import tempfile

source = Path(__file__).parent.resolve()
with tempfile.TemporaryDirectory(prefix='pcm-queue-') as tmp:
    path = Path(tmp)
    (path / 'test.cpp').write_text(r'''
#include "sc6f0-pcm-write-queue.h"
#include <cassert>
#include <cstring>
#include <deque>
#include <iostream>
struct Fake {
    std::deque<long> returns;
    std::vector<int> accepted;
    int calls = 0, prepares = 0, prepare_result = 0;
    long write(const uint8_t *data, size_t frames) {
        ++calls;
        long n = returns.empty() ? static_cast<long>(frames) : returns.front();
        if (!returns.empty()) returns.pop_front();
        if (n > 0 && static_cast<size_t>(n) <= frames)
            for (long i = 0; i < n; ++i) {
                int value; std::memcpy(&value, data + i * 8, 4);
                accepted.push_back(value);
            }
        return n;
    }
};
std::vector<uint8_t> data(size_t frames) {
    std::vector<uint8_t> bytes(frames * 8);
    for (size_t i = 0; i < frames; ++i) {
        int value = static_cast<int>(i);
        std::memcpy(bytes.data() + i * 8, &value, 4);
    }
    return bytes;
}
auto drain(std::vector<uint8_t>& q, bool& tail, Fake& fake) {
    return sc6f0_pcm::drain(q, tail,
        [&](const uint8_t *d, size_t n) { return fake.write(d, n); },
        [&]() { ++fake.prepares; return fake.prepare_result; });
}
void all_frames(const Fake& fake, int count) {
    assert(fake.accepted.size() == static_cast<size_t>(count));
    for (int i = 0; i < count; ++i) assert(fake.accepted[i] == i);
}
int main() {
    { auto q=data(1920); bool tail=false; Fake f;
      auto s=drain(q,tail,f); assert(q.empty() && s.accepted==1920); all_frames(f,1920); }
    { auto q=data(960); auto original=q; bool tail=false; Fake f; f.returns={-EAGAIN};
      auto s=drain(q,tail,f); assert(q==original && s.again==1 && f.calls==1);
      drain(q,tail,f); assert(q.empty()); all_frames(f,960); }
    { auto q=data(960); bool tail=false; Fake f; f.returns={240,-EAGAIN};
      auto s=drain(q,tail,f); assert(q.size()==720*8 && tail && s.short_writes==1);
      drain(q,tail,f); assert(q.empty() && !tail); all_frames(f,960); }
    { auto q=data(960); bool tail=false; Fake f; f.returns={240,-EPIPE};
      auto s=drain(q,tail,f); assert(q.size()==720*8 && tail && s.xruns==1);
      drain(q,tail,f); assert(q.empty() && !tail); all_frames(f,960); }
    { auto q=data(960); auto original=q; bool tail=false; Fake f; f.returns={-EPIPE};
      auto s=drain(q,tail,f); assert(q==original && s.xruns==1 && f.prepares==1 && f.calls==1);
      drain(q,tail,f); all_frames(f,960); }
    { auto q=data(960); auto original=q; bool tail=false; Fake f;
      f.returns={-ESTRPIPE}; f.prepare_result=-EIO;
      auto s=drain(q,tail,f); assert(q==original && s.suspended==1 && s.recovery_errors==1); }
    for (long result : {0L, -static_cast<long>(EINTR), -static_cast<long>(EIO), 961L}) {
      auto q=data(960); auto original=q; bool tail=false; Fake f; f.returns={result};
      drain(q,tail,f); assert(q==original && f.calls==1); }
    { auto q=data(16); bool tail=false; Fake f;
      drain(q,tail,f); assert(q.size()==16*8 && f.calls==0); }
    { auto q=data(960); bool tail=false; Fake f; f.returns=std::deque<long>(20,1);
      drain(q,tail,f); assert(f.calls==16 && q.size()==944*8 && tail);
      drain(q,tail,f); assert(q.empty()); all_frames(f,960); }
    { auto q=data(9000); bool tail=false; Fake f; f.returns={-EAGAIN};
      auto s=drain(q,tail,f); assert(q.size()==7680*8 && s.dropped==1320);
      int first; std::memcpy(&first,q.data(),4); assert(first==1320); }
    std::cout << "PASS: full/partial writes, EAGAIN, EINTR, zero, XRUN, recovery failure, "
                 "fatal/invalid returns, accumulation, retry bound and queue overflow\n";
}
''')
    subprocess.run(['c++', '-std=c++14', '-Wall', '-Wextra', '-Werror',
                    '-I', str(source), str(path / 'test.cpp'), '-o', str(path / 'test')],
                   check=True)
    subprocess.run([str(path / 'test')], check=True)
