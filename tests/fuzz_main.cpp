// A small stand-in for libFuzzer, for compilers that don't ship it (Apple
// clang, g++): mutates the seed files at random and feeds each result to the
// target's LLVMFuzzerTestOneInput, built with the address and undefined
// behavior sanitizers. Not coverage guided, so libFuzzer finds more in the
// same time; the Makefile uses libFuzzer when the compiler has it.
//
//   fuzz_x -max_total_time=60 [-seed=N] [-max_len=N] [-dict=F] [-artifact_prefix=P] <seed file or folder>...
//   fuzz_x <file>...   runs each file once (a crash file, to reproduce it)
//
// A crash leaves the input that caused it in <artifact_prefix>crash-<hash>,
// like libFuzzer.
#include <sanitizer/common_interface_defs.h>

#include <chrono>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include <unistd.h>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size);

namespace fs = std::filesystem;

namespace {

std::vector<uint8_t> current;
std::string artifact_prefix;

uint64_t fnv1a(const std::vector<uint8_t> &d) {
  uint64_t h = 1469598103934665603ull;
  for (uint8_t b : d)
    h = (h ^ b) * 1099511628211ull;
  return h;
}

// Called from a sanitizer report, abort() or the timeout alarm.
void save_current() {
  char name[512];
  snprintf(name, sizeof(name), "%scrash-%016llx", artifact_prefix.c_str(), (unsigned long long) fnv1a(current));
  FILE *f = fopen(name, "wb");
  if (f == nullptr)
    return;
  fwrite(current.data(), 1, current.size(), f);
  fclose(f);
  fprintf(stderr, "Input saved to %s (%zu bytes)\n", name, current.size());
}

// UBSan and failed assertions end in abort(); ASan reports run the death
// callback instead.
void on_abort(int) {
  save_current();
  signal(SIGABRT, SIG_DFL);
  raise(SIGABRT);
}

void on_alarm(int) {
  fprintf(stderr, "Timeout: one input ran over 10 seconds\n");
  save_current();
  _exit(1);
}

std::vector<uint8_t> slurp(const fs::path &p) {
  std::ifstream f(p, std::ios::binary);
  std::stringstream ss;
  ss << f.rdbuf();
  std::string s = ss.str();
  return std::vector<uint8_t>(s.begin(), s.end());
}

// A libFuzzer dictionary: one quoted token per line, with \\, \" and \xNN
// escapes, an optional name= in front, and # comments.
std::vector<std::string> load_dict(const char *path) {
  std::vector<std::string> out;
  std::ifstream f(path);
  std::string line;
  while (std::getline(f, line)) {
    size_t q = line.find('"');
    if (line.empty() || line[0] == '#' || q == std::string::npos)
      continue;
    std::string tok;
    for (size_t i = q + 1; i < line.size() && line[i] != '"'; i++) {
      if (line[i] != '\\' || i + 1 >= line.size()) {
        tok += line[i];
      } else if (line[i + 1] == 'x' && i + 3 < line.size()) {
        tok += (char) strtol(line.substr(i + 2, 2).c_str(), nullptr, 16);
        i += 3;
      } else {
        tok += line[++i];
      }
    }
    out.push_back(tok);
  }
  return out;
}

class Mutator {
 public:
  Mutator(uint32_t seed, std::vector<std::string> dict) : rng_(seed), dict_(std::move(dict)) {}

  size_t below(size_t n) { return n == 0 ? 0 : std::uniform_int_distribution<size_t>(0, n - 1)(this->rng_); }

  void mutate(std::vector<uint8_t> &d, const std::vector<std::vector<uint8_t>> &seeds, size_t max_len) {
    size_t rounds = 1 + this->below(8);
    for (size_t r = 0; r < rounds; r++)
      this->one(d, seeds);
    if (d.size() > max_len)
      d.resize(max_len);
  }

 private:
  void one(std::vector<uint8_t> &d, const std::vector<std::vector<uint8_t>> &seeds) {
    size_t at = this->below(d.size() + 1);
    switch (this->below(9)) {
      case 0:  // flip a bit
        if (!d.empty())
          d[this->below(d.size())] ^= (uint8_t) (1u << this->below(8));
        break;
      case 1:  // any byte
        if (!d.empty())
          d[this->below(d.size())] = (uint8_t) this->below(256);
        break;
      case 2:  // insert a byte
        d.insert(d.begin() + at, (uint8_t) this->below(256));
        break;
      case 3: {  // cut a run
        size_t n = std::min(d.size() - std::min(at, d.size()), 1 + this->below(64));
        d.erase(d.begin() + at, d.begin() + at + n);
        break;
      }
      case 4: {  // repeat a run
        if (d.empty())
          break;
        size_t from = this->below(d.size());
        size_t n = std::min(d.size() - from, 1 + this->below(256));
        std::vector<uint8_t> run(d.begin() + from, d.begin() + from + n);
        d.insert(d.begin() + at, run.begin(), run.end());
        break;
      }
      case 5:
      case 6: {  // a dictionary token
        if (this->dict_.empty())
          break;
        const std::string &t = this->dict_[this->below(this->dict_.size())];
        d.insert(d.begin() + at, t.begin(), t.end());
        break;
      }
      case 7:  // truncate, as a dropped connection does
        d.resize(at);
        break;
      default: {  // a piece of another seed
        const auto &o = seeds[this->below(seeds.size())];
        if (o.empty())
          break;
        size_t from = this->below(o.size());
        size_t n = std::min(o.size() - from, 1 + this->below(1024));
        d.insert(d.begin() + at, o.begin() + from, o.begin() + from + n);
        break;
      }
    }
  }

  std::mt19937 rng_;
  std::vector<std::string> dict_;
};

const char *flag(const char *arg, const char *name) {
  size_t n = strlen(name);
  return strncmp(arg, name, n) == 0 && arg[n] == '=' ? arg + n + 1 : nullptr;
}

}  // namespace

extern "C" const char *__ubsan_default_options() { return "print_stacktrace=1:halt_on_error=1:abort_on_error=1"; }

int main(int argc, char **argv) {
  long seconds = 0, seed = (long) time(nullptr), max_len = 1 << 20;
  std::vector<std::string> dict;
  std::vector<fs::path> paths;
  for (int i = 1; i < argc; i++) {
    const char *v;
    if ((v = flag(argv[i], "-max_total_time")) != nullptr)
      seconds = atol(v);
    else if ((v = flag(argv[i], "-seed")) != nullptr)
      seed = atol(v);
    else if ((v = flag(argv[i], "-max_len")) != nullptr)
      max_len = atol(v);
    else if ((v = flag(argv[i], "-dict")) != nullptr)
      dict = load_dict(v);
    else if ((v = flag(argv[i], "-artifact_prefix")) != nullptr)
      artifact_prefix = v;
    else if (argv[i][0] != '-')  // libFuzzer's other flags mean nothing here
      paths.push_back(argv[i]);
  }
  std::vector<std::vector<uint8_t>> seeds;
  for (const auto &p : paths) {
    if (fs::is_directory(p)) {
      for (const auto &e : fs::directory_iterator(p))
        if (e.is_regular_file())
          seeds.push_back(slurp(e.path()));
    } else {
      seeds.push_back(slurp(p));
    }
  }
  if (seeds.empty())
    seeds.emplace_back();
  __sanitizer_set_death_callback(save_current);
  signal(SIGABRT, on_abort);
  signal(SIGALRM, on_alarm);

  // Each seed as it is first, then mutants until the time is up.
  for (const auto &s : seeds) {
    current = s;
    alarm(10);
    LLVMFuzzerTestOneInput(current.data(), current.size());
  }
  if (seconds <= 0) {
    printf("Ran %zu inputs\n", seeds.size());
    return 0;
  }
  printf("Fuzzing %zu seeds for %ld s, seed %ld\n", seeds.size(), seconds, seed);
  Mutator m((uint32_t) seed, std::move(dict));
  auto end = std::chrono::steady_clock::now() + std::chrono::seconds(seconds);
  uint64_t runs = 0;
  while (std::chrono::steady_clock::now() < end) {
    for (int i = 0; i < 64; i++, runs++) {
      current = seeds[m.below(seeds.size())];
      m.mutate(current, seeds, (size_t) max_len);
      alarm(10);
      LLVMFuzzerTestOneInput(current.data(), current.size());
    }
  }
  alarm(0);
  printf("Done: %llu runs, no crash\n", (unsigned long long) runs);
  return 0;
}
