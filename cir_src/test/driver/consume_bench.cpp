/*
Part of the EDG Compiler Project, under the Apache License v2.0 with LLVM
Exceptions.
See https://edgcpp.org/LICENSE.txt for license information.
SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
*/

/*
consume_bench -- time edg_cir_session_consume called once per ECIL frame, as
the front end does, so the cost of the per-call crash-recovery guard shows
(set EDG_CIR_NO_GUARD=1 for the unguarded baseline).

  consume_bench <file.ecil>
*/

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <vector>

#include "edg_cir/edg_cir.h"

int main(int argc, char **argv)
/*
Feed the file frame by frame and print the consume and finish times.
*/
{
  int status = 2;

  if (argc == 2) {
    std::FILE *file = std::fopen(argv[1], "rb");
    std::vector<uint8_t> data;
    an_edg_cir_options options;
    an_edg_cir_session *session = nullptr;

    if (file != nullptr) {
      uint8_t buffer[65536];
      size_t n;
      while ((n = std::fread(buffer, 1, sizeof buffer, file)) > 0)
        data.insert(data.end(), buffer, buffer + n);
      std::fclose(file);
    }
    edg_cir_options_init(&options);
    options.output_kind = EDG_CIR_OUTPUT_NONE;
    status = data.size() < 16 ? 2
        : edg_cir_session_open(&options, &session) == EDG_CIR_OK ? 0 : 3;
    if (status == 0) {
      using clock = std::chrono::steady_clock;
      size_t pos = 16;
      size_t frames = 0;
      auto start = clock::now();
      bool ok = edg_cir_session_consume(session, data.data(), 16) ==
                EDG_CIR_OK;

      while (ok && pos + 8 <= data.size()) {
        uint32_t len = data[pos] | (data[pos + 1] << 8) |
                       (data[pos + 2] << 16) |
                       (static_cast<uint32_t>(data[pos + 3]) << 24);
        size_t total = 8 + len;
        ok = pos + total <= data.size() &&
             edg_cir_session_consume(session, data.data() + pos, total) ==
                 EDG_CIR_OK;
        pos += total;
        ++frames;
      }
      auto mid = clock::now();
      ok = ok && edg_cir_session_finish(session) == EDG_CIR_OK;
      auto end = clock::now();
      std::printf("frames=%zu consume=%.1f ms (%.2f us/frame) finish=%.1f ms "
                  "%s\n", frames,
                  std::chrono::duration<double, std::milli>(mid - start)
                      .count(),
                  std::chrono::duration<double, std::micro>(mid - start)
                          .count() / (frames ? frames : 1),
                  std::chrono::duration<double, std::milli>(end - mid)
                      .count(),
                  ok ? "ok" : "FAILED");
      edg_cir_session_close(session);
      status = ok ? 0 : 1;
    }
  }
  return status;
}  /* main */
