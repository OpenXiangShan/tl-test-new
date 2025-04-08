//
// Created by Kaifan Wang on 2021/10/25.
//

#include <cstddef>
#include <iterator>
#include <unordered_map>
#include <array>
#include <memory>
#include <type_traits>

#include "../Base/TLLocal.hpp"

#include "Common.h"
#include "gravity_utility.hpp"

#ifndef TLC_TEST_SCOREBOARD_H
#define TLC_TEST_SCOREBOARD_H

const int ERR_NOTFOUND = 1;
const int ERR_MISMATCH = 2;

// #define SB_DEBUG        1


#ifndef SB_DEBUG
#   define SB_DEBUG        0
#endif


template<std::size_t N>
inline void data_dump(const uint8_t* data)
{
    Gravity::StringAppender sa;

    sa.Append("[ ");

    sa.Hex().Fill('0');

    if (glbl.cfg.verbose_data_full)
    {
        for (std::size_t j = 0; j < N; j++)
            sa.NextWidth(2).Append(uint64_t(data[j]), " ");
    }
    else
    {
        bool skip = false;
        for (std::size_t j = 0; j < N;)
        {
            sa.NextWidth(2).Append(uint64_t(data[j]), " ");

            if (skip)
            {
                j = (j / BEATSIZE + 1) * BEATSIZE;
                sa.Append("... ");
            }
            else 
                j++;

            skip = !skip;
        }
    }

    sa.Append("]");

    std::cout << sa.ToString();
}

template<std::size_t N>
inline void data_dump_embedded(const uint8_t* data)
{
    if (glbl.cfg.verbose_data_full)
        std::cout << std::endl;

    data_dump<N>(data);
}

template<std::size_t N>
inline void data_dump_on_verify(const uint8_t *dut, const uint8_t *ref)
{
    std::cout << std::endl;

    std::cout << "dut: ";
    data_dump<N>(dut);
    std::cout << std::endl;

    std::cout << "ref: ";
    data_dump<N>(ref);
    std::cout << std::endl;
}


template<typename Tk, typename Tv>
struct ScoreBoardUpdateCallback {
    void update(const TLLocalContext* ctx, const Tk& key, std::shared_ptr<Tv>& data) {};
};

template<typename Tk, typename Tv>
struct ScoreBoardUpdateCallbackDefault : public ScoreBoardUpdateCallback<Tk, Tv> {
    void update(const TLLocalContext* ctx, const Tk& key, std::shared_ptr<Tv>& data) {};
};

template<typename Tk, typename Tv, typename TUpdateCallback = ScoreBoardUpdateCallbackDefault<Tk, Tv>>
class ScoreBoard {
    friend class ScoreBoardUpdateCallback<Tk, Tv>;
protected:
    TUpdateCallback                             updateCallback;
    std::unordered_map<Tk, std::shared_ptr<Tv>> mapping;
public:
    ScoreBoard();
    ~ScoreBoard();
    std::unordered_map<Tk, std::shared_ptr<Tv>>& get();
    void update(const TLLocalContext* ctx,const Tk& key, std::shared_ptr<Tv>& data);
    std::shared_ptr<Tv> query(const TLLocalContext* ctx, const Tk& key);
    void erase(const TLLocalContext* ctx, const Tk& key);
    int verify(const Tk& key, const Tv& data) const;
    bool haskey(const Tk& key);
};


class Global_SBEntry {
public:
    enum {
        SB_INVALID = 0,
        SB_VALID,
        SB_PENDING
    };
    int status;
    shared_tldata_t<DATASIZE>   data;
    shared_tldata_t<DATASIZE>   pending_data; // used for put&release
};

template<typename Tk>
struct ScoreBoardUpdateCallbackGlobalEntry : public ScoreBoardUpdateCallback<Tk, Global_SBEntry>
{
    void update(const TLLocalContext* ctx, const Tk& key, std::shared_ptr<Global_SBEntry>& data)
    {
#       if SB_DEBUG == 1
            Gravity::StringAppender strapp;

            strapp.ShowBase()
                .Hex().Append("key = ", uint64_t(key));

            strapp.Append(", type = Global_SBEntry");

            strapp.Append(", status = ");
            switch (data->status)
            {
                case Global_SBEntry::SB_INVALID:    strapp.Append("SB_INVALID");    break;
                case Global_SBEntry::SB_VALID:      strapp.Append("SB_VALID");      break;
                case Global_SBEntry::SB_PENDING:    strapp.Append("SB_PENDING");    break;
                default:
                    strapp.Append("<unknown:", uint64_t(data->status), ">");
                    break;
            }

            strapp.EndLine();

            Debug(ctx, Append(strapp.ToString()));

            std::cout << "data - data : ";
            if (data->data != nullptr)
                data_dump<DATASIZE>(data->data->data);
            else
                std::cout << "<non-initialized>";
            std::cout << std::endl;

            std::cout << "data - pend : ";
            if (data->pending_data != nullptr)
                data_dump<DATASIZE>(data->pending_data->data);
            else
                std::cout << "<non-initialized>";
            std::cout << std::endl;
#       endif
    }
};


template<typename T>
class GlobalBoard : public ScoreBoard<T, Global_SBEntry, ScoreBoardUpdateCallbackGlobalEntry<T>> {
private:
    int data_check(TLLocalContext* ctx, const uint8_t* dut, const uint8_t* ref, std::string assert_info);
    uint8_t init_zeros[DATASIZE];
public:
    GlobalBoard() noexcept;
    int verify(TLLocalContext* ctx, const T& key, shared_tldata_t<DATASIZE> data);
    void unpending(TLLocalContext* ctx, const T& key);
};


/************************** Implementation **************************/
template<typename Tk, typename Tv, typename TUpdateCallback>
ScoreBoard<Tk, Tv, TUpdateCallback>::ScoreBoard() {
    mapping.clear();
}

template<typename Tk, typename Tv, typename TUpdateCallback>
ScoreBoard<Tk, Tv, TUpdateCallback>::~ScoreBoard() {
}

template<typename Tk, typename Tv, typename TUpdateCallback>
std::unordered_map<Tk, std::shared_ptr<Tv>>& ScoreBoard<Tk, Tv, TUpdateCallback>::get() {
    return this->mapping;
}

template<typename Tk, typename Tv, typename TUpdateCallback>
void ScoreBoard<Tk, Tv, TUpdateCallback>::update(const TLLocalContext* ctx, const Tk& key, std::shared_ptr<Tv>& data) {
    if (mapping.count(key) != 0) {
        mapping[key] = data;
    } else {
        mapping.insert(std::make_pair(key, data));
    }

    updateCallback.update(ctx, key, data);
}

template<typename Tk, typename Tv, typename TUpdateCallback>
std::shared_ptr<Tv> ScoreBoard<Tk, Tv, TUpdateCallback>::query(const TLLocalContext* ctx, const Tk& key) {
    if (mapping.count(key) > 0) {
        return mapping[key];
    } else {
        tlc_assert(false, ctx, 
            Gravity::StringAppender().Hex().ShowBase()
                .Append("Key no found: ", uint64_t(key))
            .ToString());
    }
}

template<typename Tk, typename Tv, typename TUpdateCallback>
void ScoreBoard<Tk, Tv, TUpdateCallback>::erase(const TLLocalContext* ctx, const Tk& key) {
    int num = mapping.erase(key);
    tlc_assert(num == 1, ctx, "Multiple value mapped to one key!");
}

template<typename Tk, typename Tv, typename TUpdateCallback>
int ScoreBoard<Tk, Tv, TUpdateCallback>::verify(const Tk& key, const Tv& data) const {
    if (mapping.count(key) > 0) {
        if (*mapping[key] != data) {
            return ERR_MISMATCH;
        }
        return 0;
    }
    return ERR_NOTFOUND;
}

template<typename Tk, typename Tv, typename TUpdateCallback>
bool ScoreBoard<Tk, Tv, TUpdateCallback>::haskey(const Tk &key) {
    return mapping.count(key) > 0;
}

template<typename T>
GlobalBoard<T>::GlobalBoard() noexcept
{
    std::memset(init_zeros, 0, DATASIZE);
}


template<typename T>
int GlobalBoard<T>::data_check(TLLocalContext* ctx, const uint8_t *dut, const uint8_t *ref, std::string assert_info) {
    for (int i = 0; i < DATASIZE; i++) {
        if (dut[i] != ref[i]) {
            data_dump_on_verify<DATASIZE>(dut, ref);
            tlc_assert(false, ctx, assert_info.data());
            return -1;
        }
    }
    return 0;
}

template<typename T>
int GlobalBoard<T>::verify(TLLocalContext* ctx, const T& key, shared_tldata_t<DATASIZE> data) {
    if (this->mapping.count(key) == 0) { // we assume data is all zero initially
        return this->data_check(ctx, data->data, init_zeros, "Init data is non-zero!");
    }
    tlc_assert(this->mapping.count(key) == 1, ctx, "Duplicate records found in GlobalBoard!");

    Global_SBEntry value = *this->mapping.at(key).get();
    if (value.status == Global_SBEntry::SB_VALID) {
        tlc_assert(value.data != nullptr, ctx, "NULL occured in valid entry of GlobalBoard!");
        return this->data_check(ctx, data->data, value.data->data, "Data mismatch from status SB_VALID!");
    } else if (value.status == Global_SBEntry::SB_PENDING) {
        bool flag = true;
        if (value.data != nullptr) {
            for (int i = 0; i < DATASIZE; i++) {
                if (data->data[i] != value.data->data[i]) {
                    flag = false;
                    break;
                }
            }
            if (flag) return 0;
        } else {
            for (int i = 0; i < DATASIZE; i++) {
                if (data->data[i] != init_zeros[i]) {
                    flag = false;
                    break;
                }
            }
            if (flag) return 0;
        }
        tlc_assert(value.pending_data != nullptr, ctx, "NULL occured in pending entry of GlobalBoard!");
        this->data_check(ctx, data->data, value.pending_data->data, "Data mismatch from status SB_PENDING!");
        return 0;
    } else {
        // TODO: handle other status
        tlc_assert(false, ctx, "Unknown GlobalBoard entry status!");
        return -1;
    }
}

template<typename T>
void GlobalBoard<T>::unpending(TLLocalContext* ctx, const T& key) {
    tlc_assert(this->mapping.count(key) == 1, ctx, "Un-pending non-exist entry in GlobalBoard!");
    Global_SBEntry* value = this->mapping.at(key).get();
    // tlc_assert(value->pending_data != nullptr, "Un-pending entry with NULL ptr in GlobalBoard!");
    if (value->pending_data == nullptr) {
        return;
    }
    value->data = value->pending_data;
    value->pending_data = nullptr;
    value->status = Global_SBEntry::SB_VALID;
}

#endif // TLC_TEST_SCOREBOARD_H
