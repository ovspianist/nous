#include <chrono>
#include <cstdio>
#include <cstring>

// Standalone bench: uses the Liang headers from the main project directly.
// Build separately (not part of the GTest suite) — see hyphenation/CMakeLists.txt
// for the original standalone build. To rebuild:
//   cd hyphenation && cmake --build build2 --config Release --target bench_hyphenation
#include "microreader/content/hyphenation/Liang/hyph-de.h"
#include "microreader/content/hyphenation/Liang/hyph-en-us.h"
#include "microreader/content/hyphenation/Liang/liang_hyphenation_patterns.h"

// Minimal shim: the standalone bench calls hyphenate() directly; here we call
// liang_hyphenate() via a thin wrapper that matches the same signature.
static int hyphenate(const char* word, int leftmin, int rightmin, char bc, int* out, int maxout,
                     const HyphenationPatterns& pats, const HyphenationCharIndex* cidx = nullptr);
// (implementation below word lists)

// Representative words for benchmarking — a mix of short, medium, and long
// words in English and German that exercise both the "no hit" and "hyphenated"
// paths of the algorithm.
static const char* kEnglishWords[] = {
    "hyphenation",
    "algorithm",
    "implementation",
    "internationalization",
    "uncharacteristically",
    "incomprehensibility",
    "counterproductive",
    "the",
    "and",
    "is",
    "beautiful",
    "programming",
    "performance",
    "optimization",
    "dictionary",
    "understanding",
    "overwhelming",
    "extraordinary",
    "concatenation",
    "administration",
    "communication",
    "multiplication",
    "approximately",
    "unfortunately",
    "conscientious",
    "predetermined",
    "simultaneously",
    "straightforward",
    "acknowledgement",
    "establishment",
};

static const char* kGermanWords[] = {
    "Hyphenierung",
    "Algorithmus",
    "Implementierung",
    "Internationalisierung",
    "Geschwindigkeitsbegrenzung",
    "Bundesverfassungsgericht",
    "Kraftfahrzeug",
    "die",
    "und",
    "ist",
    "schoen",
    "Programmierung",
    "Leistung",
    "Optimierung",
    "Woerterbuch",
    "Verstaendnis",
    "ueberwaetigend",
    "ausserordentlich",
    "Verkehrsinfrastruktur",
    "Datenschutzbeauftragter",
    "Rindfleischetikettierung",
    "Donaudampfschiffahrtsgesellschaft",
    "Rechtsschutzversicherung",
    "Grundgesetzaenderung",
    "Sozialversicherungspflichtig",
};

static constexpr int kIterations = 100000;

static double bench(const char* const* words, int nwords, const HyphenationPatterns& pats,
                    const HyphenationCharIndex* cidx, const char* label) {
  int positions[32];
  volatile int sink = 0;

  auto t0 = std::chrono::high_resolution_clock::now();
  for (int iter = 0; iter < kIterations; ++iter) {
    for (int i = 0; i < nwords; ++i) {
      sink += hyphenate(words[i], 2, 2, '.', positions, 32, pats, cidx);
    }
  }
  auto t1 = std::chrono::high_resolution_clock::now();
  (void)sink;

  double ms = std::chrono::duration<double, std::milli>(t1 - t0).count();
  long long total_calls = (long long)kIterations * nwords;
  double us_per_call = ms * 1000.0 / (double)total_calls;
  std::printf("  %-16s  words=%d  total=%.1fms  per_call=%.3fus\n", label, nwords, ms, us_per_call);
  return ms;
}

int main() {
  int en_words = (int)(sizeof(kEnglishWords) / sizeof(kEnglishWords[0]));
  int de_words = (int)(sizeof(kGermanWords) / sizeof(kGermanWords[0]));

  std::printf("EN patterns: %zu  DE patterns: %zu  iters: %d\n\n", en_us_patterns.count, de_patterns.count,
              kIterations);

  std::printf("--- EN ---\n");
  double en_base = bench(kEnglishWords, en_words, en_us_patterns, nullptr, "baseline");
  double en_idx = bench(kEnglishWords, en_words, en_us_patterns, &en_us_char_idx, "char-index");
  std::printf("  speedup: %.2fx\n\n", en_base / en_idx);

  std::printf("--- DE ---\n");
  double de_base = bench(kGermanWords, de_words, de_patterns, nullptr, "baseline");
  double de_idx = bench(kGermanWords, de_words, de_patterns, &de_char_idx, "char-index");
  std::printf("  speedup: %.2fx\n\n", de_base / de_idx);

  return 0;
}
