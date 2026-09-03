#include "dictionary/bilingual_dictionary.h"

#include <algorithm>
#include <chrono>
#include <iostream>
#include <string_view>
#include <vector>

int main(int argc, char **argv) {
  if (argc != 2) {
    std::cerr << "usage: dictionary_benchmark DICTIONARY.tsv\n";
    return 2;
  }
  const auto dictionary = bilingual::BilingualDictionary::loadTsv(argv[1]);
  constexpr std::size_t iterations = 200000;
  constexpr std::string_view words[] = {"你好", "方法", "方案", "不存在"};
  std::vector<long long> samples;
  samples.reserve(iterations);
  std::size_t hits = 0;
  for (std::size_t i = 0; i < iterations; ++i) {
    const auto start = std::chrono::steady_clock::now();
    hits += dictionary.lookup(words[i % std::size(words)]) != nullptr;
    const auto end = std::chrono::steady_clock::now();
    samples.push_back(
        std::chrono::duration_cast<std::chrono::nanoseconds>(end - start)
            .count());
  }
  std::sort(samples.begin(), samples.end());
  const auto percentile = [&](double p) {
    return samples[static_cast<std::size_t>(p * (samples.size() - 1))];
  };
  std::cout << "entries=" << dictionary.size() << " iterations=" << iterations
            << " hits=" << hits << " p50_ns=" << percentile(0.50)
            << " p95_ns=" << percentile(0.95) << " p99_ns=" << percentile(0.99)
            << '\n';
}
