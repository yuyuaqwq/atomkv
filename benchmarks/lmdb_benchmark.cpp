//The MIT License(MIT)
//Copyright ? 2024 https://github.com/yuyuaqwq
//
//Permission is hereby granted, free of charge, to any person obtaining a copy of this software and associated documentation files(the ��Software��), to deal in the Software without restriction, including without limitation the rights to use, copy, modify, merge, publish, distribute, sublicense, and /or sell copies of the Software, and to permit persons to whom the Software is furnished to do so, subject to the following conditions :
//
//The above copyright notice and this permission notice shall be included in all copies or substantial portions of the Software.
//
//THE SOFTWARE IS PROVIDED ��AS IS��, WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include <filesystem>

#include <lmdb.h>

#include "util/test_util.h"

namespace atomkv {

static int FLAGS_num = 1000000;

static std::string_view FLAGS_benchmarks =
    //"fillseq,"
    //"readseq,"
    //"fillsync,"
    //"fillseqbatch,"
    //"fillrandom,"
    //"readrandom,"
    "fillrandbatch,"
    //"readrandbatch,"
    //"overwrite,"
    //"overwritebatch,"
    //"fillrand100K,"
    ////"readrand100K,"
    //"fillseq100K,"
;

#define E(expr) CHECK((rc = (expr)) == MDB_SUCCESS, #expr)
#define RES(err, expr) ((rc = expr) == (err) || (CHECK(!rc, #expr), 0))
#define CHECK(test, msg) ((test) ? (void)0 : ((void)fprintf(stderr, \
	"%s:%d: %s: %s\n", __FILE__, __LINE__, msg, mdb_strerror(rc)), abort()))

class Benchmark {
private:
    MDB_env* env_ = nullptr;
    int seed_ = 0 ;
    int num_ = FLAGS_num;
    
    std::chrono::steady_clock::time_point start_;
    int64_t bytes_;
    int done_;
    int next_report_;

    const int kKeySize = 16;
    const int kValueSize = 100;
    std::vector<std::string> seq_key_;
    std::vector<std::string> seq_value_;
    std::vector<std::string> rand_key_;
    std::vector<std::string> rand_value_;
    std::vector<std::string> seq_value_100k_;
    std::vector<std::string> rand_value_100k_;

    void PrintEnvironment() {
        std::fprintf(stderr, "lmdb:     version %s\n", MDB_VERSION_STRING);
    }

    void PrintHeader() {
        PrintEnvironment();
        std::fprintf(stdout, "Keys:       %d bytes each\n", kKeySize);
        std::fprintf(stdout, "Values:     %d bytes each\n", kValueSize);
        std::fprintf(stdout, "Entries:    %d\n", num_);
        std::fprintf(stdout, "------------------------------------------------\n");
    }

    void Open(bool sync) {
        if (env_) {
            mdb_env_close(env_);
        }
        std::string path = "./lmdb_benchmark";
        std::filesystem::remove_all(path);
        std::filesystem::create_directory(path);
        int rc;
        E(mdb_env_create(&env_));
        E(mdb_env_set_maxreaders(env_, 1));
        E(mdb_env_set_mapsize(env_, 1024 * 1024 * 256));
        E(mdb_env_open(env_, path.c_str(), MDB_FIXEDMAP | sync ? 0 : MDB_NOSYNC, 0664));
    }

    void Start() {
        start_ = std::chrono::high_resolution_clock::now();
        done_ = 0;
        next_report_ = 100;
        bytes_ = 0;
    }

    void FinishedSingleOp() {
        done_++;
        if (done_ >= next_report_) {
            if (next_report_ < 1000)
                next_report_ += 100;
            else if (next_report_ < 5000)
                next_report_ += 500;
            else if (next_report_ < 10000)
                next_report_ += 1000;
            else if (next_report_ < 50000)
                next_report_ += 5000;
            else if (next_report_ < 100000)
                next_report_ += 10000;
            else if (next_report_ < 500000)
                next_report_ += 50000;
            else
                next_report_ += 100000;
            std::fprintf(stderr, "... finished %d ops%30s\r", done_, "");
            std::fflush(stderr);
        }
    }

    void Stop(std::string_view name) {
        auto finish = std::chrono::high_resolution_clock::now();
        auto duration = std::chrono::duration_cast<std::chrono::microseconds>(finish - start_);

        // Pretend at least one op was done in case we are running a benchmark
        // that does not call FinishedSingleOp().
        if (done_ < 1) done_ = 1;

        std::string rate;
        if (bytes_ > 0) {
            rate = std::format("{:6.1f} MB/s", (bytes_ / 1048576.0) / (duration.count() / 1e6));
            if (done_ != num_) {
                rate += std::format(" ({}ops)", done_);
            }
        }

        std::fprintf(stdout, "%-12s : %11.3f micros/op; %s\n",
            std::string(name.data(), name.size()).c_str(), double(duration.count()) / done_, rate.c_str());
        std::fflush(stdout);
    }

public:
    enum Order { SEQUENTIAL, RANDOM };
    enum DBState { FRESH, EXISTING };

    void Run() {
        PrintHeader();

        srand(seed_);
        seq_key_.resize(num_);
        seq_value_.resize(num_);
        for (auto i = 0; i < num_; i++) {
            seq_key_[i] = std::format("{:016d}", i);
            seq_value_[i] = std::format("{:0100d}", i);
        }
        rand_key_.resize(num_);
        rand_value_.resize(num_);
        for (auto i = 0; i < num_; i++) {
            rand_key_[i] = atomkv::RandomString(kKeySize, kKeySize);
            rand_value_[i] = atomkv::RandomString(kValueSize, kValueSize);
        }
        seq_value_100k_.resize(num_ / 1000);
        rand_value_100k_.resize(num_ / 1000);
        for (auto i = 0; i < num_ / 1000; i++) {
            seq_value_100k_[i] = std::format("{:0100000d}", i);
            rand_value_100k_[i] = atomkv::RandomString(kValueSize * 1000, kValueSize * 1000);
        }

        auto benchmarks = FLAGS_benchmarks;
        while (!benchmarks.empty()) {
            auto pos = benchmarks.find(',');
            std::string_view name;
            if (pos != -1) {
                name = benchmarks.substr(0, pos);
                benchmarks = benchmarks.substr(pos + 1);
                
            } else {
                name = benchmarks;
                benchmarks = {};
            }

            Start();

            bool write_sync = false;
            if (name == "fillseq") {
                Write(write_sync, SEQUENTIAL, FRESH, seq_key_, seq_value_, num_, 1);
            } else if(name == "fillsync") {
                Write(true, SEQUENTIAL, FRESH, seq_key_, seq_value_, num_ / 100, 1);
            } else if(name == "fillseqbatch") {
                Write(write_sync, SEQUENTIAL, FRESH, seq_key_, seq_value_, num_, 1000);
            } else if (name == "fillrandom") {
                Write(write_sync, RANDOM, FRESH, rand_key_, rand_value_, num_, 1);
            } else if (name == "fillrandbatch") {
                Write(write_sync, RANDOM, FRESH, rand_key_, rand_value_, num_, num_);
            } else if (name == "overwrite") {
                Write(write_sync, RANDOM, EXISTING, rand_key_, rand_value_, num_, 1);
            } else if (name == "overwritebatch") {
                Write(write_sync, RANDOM, EXISTING, rand_key_, rand_value_, num_, 1000);
            } else if (name == "readseq") {
                ReadSequential();
            } else if (name == "readrandom") {
                Read(RANDOM, rand_key_, rand_value_, num_, 1);
            } else if (name == "readrandbatch") {
                Read(RANDOM, rand_key_, rand_value_, num_, 1000);
            } else if (name == "fillseq100K") {
                Write(write_sync, SEQUENTIAL, FRESH, seq_key_, seq_value_100k_, num_ / 1000, 1);
            } else if (name == "fillrand100K") {
                Write(write_sync, RANDOM, FRESH, rand_key_, rand_value_100k_, num_ / 1000, 1);
            } else if (name == "readrand100K") {
                Read(RANDOM, rand_key_, rand_value_100k_, num_ / 1000, 1);
            }
            Stop(name);
        }
    }

    void Write(bool write_sync, Order order, DBState db_state, const std::vector<std::string>& key, const std::vector<std::string>& value, int num_entries, int entries_per_batch) {
        if (db_state == FRESH) {
            Open(write_sync);
        }

        int rc;
        MDB_dbi dbi;
        MDB_val mdb_key, mdb_data;
        MDB_txn* txn;
        for (int i = 0; i < num_entries; i += entries_per_batch) {
            E(mdb_txn_begin(env_, NULL, 0, &txn));
            E(mdb_dbi_open(txn, NULL, 0, &dbi));
            for (int j = 0; j < entries_per_batch; j++) {
                mdb_key.mv_size = key[i + j].size();
                mdb_key.mv_data = const_cast<void*>(reinterpret_cast<const void*>(key[i + j].data()));
                mdb_data.mv_size = value[i + j].size();
                mdb_data.mv_data = const_cast<void*>(reinterpret_cast<const void*>(value[i + j].data()));
                RES(MDB_KEYEXIST, mdb_put(txn, dbi, &mdb_key, &mdb_data, 0 /*MDB_NOOVERWRITE*/));
                bytes_ += key[i+j].size() + value[i+j].size();
                FinishedSingleOp();
            }
            mdb_dbi_close(env_, dbi);
            E(mdb_txn_commit(txn));
        }
    }

    void Read(Order order, const std::vector<std::string>& key, const std::vector<std::string>& value, int num_entries, int entries_per_batch) {
        int rc;
        MDB_dbi dbi;
        MDB_val mdb_key, mdb_data;
        MDB_txn* txn;
        for (int i = 0; i < num_entries; i += entries_per_batch) {
            MDB_dbi dbi;
            MDB_val mdb_key, mdb_data;
            MDB_txn* txn;
            MDB_stat mst;
            E(mdb_txn_begin(env_, NULL, MDB_RDONLY, &txn));
            E(mdb_dbi_open(txn, NULL, 0, &dbi));
            for (int j = 0; j < entries_per_batch; j++) {
                mdb_key.mv_size = key[i + j].size();
                mdb_key.mv_data = const_cast<void*>(reinterpret_cast<const void*>(key[i + j].data()));
                mdb_data.mv_size = value[i + j].size();
                mdb_data.mv_data = const_cast<void*>(reinterpret_cast<const void*>(value[i + j].data()));
                auto res = mdb_get(txn, dbi, &mdb_key, &mdb_data);
                if (!res) {
                    bytes_ += mdb_key.mv_size + mdb_data.mv_size;
                }
                FinishedSingleOp();
            }
            mdb_dbi_close(env_, dbi);
            mdb_txn_abort(txn);
        }
    }

    void ReadSequential() {
        int rc;
        MDB_dbi dbi;
        MDB_val mdb_key, mdb_data;
        MDB_txn* txn;
        MDB_cursor* cursor;

        E(mdb_txn_begin(env_, NULL, MDB_RDONLY, &txn));
        E(mdb_open(txn, NULL, 0, &dbi));

        // �����α�
        E(mdb_cursor_open(txn, dbi, &cursor));
        while (mdb_cursor_get(cursor, &mdb_key, &mdb_data, MDB_NEXT) == MDB_SUCCESS) {
            bytes_ += mdb_key.mv_size + mdb_data.mv_size;
            FinishedSingleOp();
        }
        mdb_dbi_close(env_, dbi);
        mdb_txn_abort(txn);
    }
};

} // namespace atomkv_benchmark

int main() {
    atomkv::Benchmark benchmark;
    benchmark.Run();
}