#pragma once

#include "Common.h"
#include "ScoreBoard.h"
#include "gravity_utility.hpp"

#ifndef TLC_TEST_ULSCOREBOARD_H
#define TLC_TEST_ULSCOREBOARD_H


template<std::size_t N>
inline void data_dump_multiple_on_verify(const uint8_t* dut, const std::vector<shared_tldata_t<N>>& ref)
{
    std::cout << std::endl;

    std::cout << "DUT [    ]: ";
    data_dump<N>(dut);
    std::cout << std::endl;

    for (int i = 0; i < ref.size(); i++)
    {
        std::cout << "REF [" 
            << Gravity::StringAppender().Dec().NextWidth(4).Right().Append(i).ToString() 
            << "]: ";
        data_dump<N>(ref[i]->data);
        std::cout << std::endl;
    }
}

template<>
struct std::hash<std::array<paddr_t, 2>> {
    auto operator()(const std::array<paddr_t, 2>& key) const {
        return key[0] ^ key[1];
    }
};

/*
* *NOTICE: All corresponding data between Get and AccessAckData was recorded and allowed.
*/
template<typename Tk, typename Tv>
struct ULScoreBoardCallback {
    void    allocate(const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source, std::shared_ptr<Tv> data) {};
    void    append  (const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source, std::shared_ptr<Tv> data) {};
    void    finish  (const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source) {};
};

template<typename Tk, typename Tv>
struct ULScoreBoardCallbackDefault : public ULScoreBoardCallback<Tk, Tv> {
    void    allocate(const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source, std::shared_ptr<Tv> data) {};
    void    append  (const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source, std::shared_ptr<Tv> data, int ordinal) {};
    void    finish  (const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source) {};
};

template<typename Tk, typename Tv, typename TCallback = ULScoreBoardCallbackDefault<Tk, Tv>>
class ULScoreBoard {
    friend class ULScoreBoardCallback<Tk, Tv>;
protected:
    TCallback                                                   callback;
    size_t                                                      count;
    std::unordered_map<Tk, std::unordered_map<uint32_t, std::vector<std::shared_ptr<Tv>>>>*   mapping;
public:
    ULScoreBoard(size_t ulAgentCount);
    ~ULScoreBoard();
    void    allocate    (const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source, std::shared_ptr<Tv> data);
    void    append      (const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source,std::shared_ptr<Tv> data);
    void    appendAll   (const TLLocalContext* ctx, const Tk& key, std::shared_ptr<Tv>& data);
    void    finish      (const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source);
    int     verify      (size_t sysId, const Tk& key, uint32_t source, const Tv& data) const;
    bool    haskey      (size_t sysId, const Tk& key, uint32_t source);
};

template<typename Tk>
struct ULScoreBoardCallbackTLData : public ULScoreBoardCallback<Tk, wrapped_tldata_t<DATASIZE>> {
    void    allocate(const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source, shared_tldata_t<DATASIZE> data);
    void    append  (const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source, shared_tldata_t<DATASIZE> data, int ordinal);
    void    finish  (const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source);
};

template<typename Tk>
class UncachedBoard : public ULScoreBoard<Tk, wrapped_tldata_t<DATASIZE>, ULScoreBoardCallbackTLData<Tk>> {
private:
    bool data_check(TLLocalContext* ctx, const uint8_t* dut, const uint8_t* ref);
    shared_tldata_t<DATASIZE>   init_zeros;
public:
    UncachedBoard(size_t ulAgentCount) noexcept;
    void    allocateZero(const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source);
    int     verify(TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source, shared_tldata_t<DATASIZE> data);
};


/************************** Implementation **************************/
template<typename Tk>
void ULScoreBoardCallbackTLData<Tk>::allocate(const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source, shared_tldata_t<DATASIZE> data)
{
#   if SB_DEBUG == 1
        Gravity::StringAppender strapp("Uncached ScoreBoard: ");

        strapp.Append("action = allocate")
            .Dec().Append(", index = ", sysId)
            .ShowBase()
            .Hex().Append(", source = ", source)
            .Hex().Append(", key = ", uint64_t(key))
            .EndLine();

        Debug(ctx, Append(strapp.ToString()));

        std::cout << "data[0]: ";
        if (data != nullptr)
            data_dump<DATASIZE>(data->data);
        else
            std::cout << "<non-initialized>";
        std::cout << std::endl;
#   endif
}

template<typename Tk>
void ULScoreBoardCallbackTLData<Tk>::append(const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source, shared_tldata_t<DATASIZE> data, int ordinal)
{
#   if SB_DEBUG == 1
        Gravity::StringAppender strapp("Uncached ScoreBoard: ");

        strapp.Append("action = append")
            .Dec().Append(", index = ", sysId)
            .ShowBase()
            .Hex().Append(", source = ", source)
            .Hex().Append(", key = ", uint64_t(key))
            .EndLine();

        Debug(ctx, Append(strapp.ToString()));

        std::cout << "data[" << ordinal << "]: ";
        if (data != nullptr)
            data_dump<DATASIZE>(data->data);
        else
            std::cout << "<non-initialized>";
        std::cout << std::endl;
#   endif
}

template<typename Tk>
void ULScoreBoardCallbackTLData<Tk>::finish(const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source)
{
#   if SB_DEBUG == 1
        Gravity::StringAppender strapp("Uncached ScoreBoard: ");

        strapp.Append("action = finish")
            .Dec().Append(", index = ", sysId)
            .ShowBase()
            .Hex().Append(", source = ", source)
            .Hex().Append(", key = ", uint64_t(key))
            .EndLine();
        
        Debug(ctx, Append(strapp.ToString()));
#   endif
}


/************************** Implementation **************************/
template<typename Tk, typename Tv, typename TUpdateCallback>
ULScoreBoard<Tk, Tv, TUpdateCallback>::ULScoreBoard(size_t ulAgentCount)
{
    count   = ulAgentCount;
    mapping = new std::unordered_map<Tk, std::unordered_map<uint32_t, std::vector<std::shared_ptr<Tv>>>>[ulAgentCount];
}

template<typename Tk, typename Tv, typename TUpdateCallback>
ULScoreBoard<Tk, Tv, TUpdateCallback>::~ULScoreBoard()
{
    delete[] mapping;
}

template<typename Tk, typename Tv, typename TUpdateCallback>
void ULScoreBoard<Tk, Tv, TUpdateCallback>::allocate(const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source, std::shared_ptr<Tv> data)
{
    if (mapping[sysId].count(key) != 0 && mapping[sysId][key].count(source) != 0)
        tlc_assert(false, ctx, Gravity::StringAppender()
            .Append("duplicated uncached scoreboard entry allocation ")
            .Append("at ", sysId)
            .ToString());
    else
    {
        if (mapping[sysId].count(key) == 0)
            mapping[sysId].insert(std::make_pair(key, std::unordered_map<uint32_t, std::vector<shared_tldata_t<DATASIZE>>>()));

        mapping[sysId][key].insert(std::make_pair(source, std::vector<shared_tldata_t<DATASIZE>>()));

        mapping[sysId][key][source].push_back(data);
    }

    callback.allocate(ctx, sysId, key, source, data);
}

template<typename Tk, typename Tv, typename TUpdateCallback>
void ULScoreBoard<Tk, Tv, TUpdateCallback>::append(const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source, std::shared_ptr<Tv> data)
{
    int ordinal = 0;
    if (mapping[sysId].count(key) != 0)
    {
        std::unordered_map<uint32_t, std::vector<std::shared_ptr<Tv>>>& sources = mapping[sysId][key];
        if (sources.count(source) != 0)
        {
            std::vector<std::shared_ptr<Tv>>& entry = sources[source];
            ordinal = entry.size();
            entry.push_back(data);
        }
        else
        {
            tlc_assert(false, ctx, Gravity::StringAppender()
            .Append("append on non-allocated scoreboard entry ")
            .Append("at ", sysId)
            .Append(" with source ", source)
            .ToString());
        }
    }
    else
        tlc_assert(false, ctx, Gravity::StringAppender()
            .Append("append on non-allocated scoreboard entry ")
            .Append("at ", sysId)
            .ToString());
    
    callback.append(ctx, sysId, key, source, data, ordinal);
}

template<typename Tk, typename Tv, typename TUpdateCallback>
void ULScoreBoard<Tk, Tv, TUpdateCallback>::appendAll(const TLLocalContext* ctx, const Tk& key, std::shared_ptr<Tv>& data)
{
    for (size_t sysId = 0; sysId < count; sysId++)
    {
        if (mapping[sysId].count(key) != 0)
        {
            std::unordered_map<uint32_t, std::vector<std::shared_ptr<Tv>>>& sources = mapping[sysId][key];
            for (auto iter = sources.begin(); iter != sources.end(); iter++)
            {
                int ordinal = iter->second.size();
                iter->second.push_back(data);
                callback.append(ctx, sysId, key, iter->first, data, ordinal);
            }
        }
        else
            continue;
    }
}

template<typename Tk, typename Tv, typename TUpdateCallback>
void ULScoreBoard<Tk, Tv, TUpdateCallback>::finish(const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source)
{
    tlc_assert(mapping[sysId].count(key) == 1, ctx, Gravity::StringAppender()
        .Append("no or multiple uncached scoreboard entry mapped ")
        .Append("at ", sysId)
        .ToString());

    tlc_assert(mapping[sysId][key].count(source) == 1, ctx, Gravity::StringAppender()
        .Append("no or multiple uncached scoreboard entry mapped ")
        .Append("at ", sysId)
        .Append(" with source ", source)
        .ToString());

    mapping[sysId][key].erase(source);

    if (mapping[sysId][key].empty())
        mapping[sysId].erase(key);
}

template<typename Tk, typename Tv, typename TUpdateCallback>
int ULScoreBoard<Tk, Tv, TUpdateCallback>::verify(size_t sysId, const Tk& key, uint32_t source, const Tv& data) const
{
    if (mapping[sysId].count(key) > 0)
    {
        if (mapping[sysId][key][source] > 0)
        {
            if (*mapping[sysId][key][source] != data)
                return ERR_MISMATCH;
        }
        else
            return ERR_NOTFOUND;

        return 0;
    }

    return ERR_NOTFOUND;
}

template<typename Tk, typename Tv, typename TUpdateCallback>
bool ULScoreBoard<Tk, Tv, TUpdateCallback>::haskey(size_t sysId, const Tk& key, uint32_t source)
{
    return mapping[sysId].count(key) > 0 && mapping[sysId][key].count(source) > 0;
}

template<typename Tk>
UncachedBoard<Tk>::UncachedBoard(size_t ulAgentCount) noexcept
    : ULScoreBoard<Tk, wrapped_tldata_t<DATASIZE>, ULScoreBoardCallbackTLData<Tk>>(ulAgentCount)
{
    init_zeros = make_shared_tldata<DATASIZE>();
    std::memset(init_zeros->data, 0, DATASIZE);
}

template<typename Tk>
void UncachedBoard<Tk>::allocateZero(const TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source)
{
    this->allocate(ctx, sysId, key, source, init_zeros);
}

template<typename Tk>
bool UncachedBoard<Tk>::data_check(TLLocalContext* ctx, const uint8_t *dut, const uint8_t *ref)
{
    for (int i = 0; i < DATASIZE; i++)
        if (dut[i] != ref[i])
            return false;
    
    return true;
}

template<typename Tk>
int UncachedBoard<Tk>::verify(TLLocalContext* ctx, size_t sysId, const Tk& key, uint32_t source, shared_tldata_t<DATASIZE> data)
{
    if (this->mapping[sysId].count(key) == 0 || this->mapping[sysId][key].count(source) == 0)
        tlc_assert(false, ctx, "non-exist data verify action");

    bool hit = false;
    std::vector<shared_tldata_t<DATASIZE>> entry = this->mapping[sysId][key][source];
    for (auto ref : entry)
        if (data_check(ctx, data->data, ref->data))
        {
            hit = true;
            break;
        }

    if (!hit)
    {
        data_dump_multiple_on_verify<DATASIZE>(data->data, entry);
        tlc_assert(false, ctx, "Data mismatch");
    }

    return 0;
}

#endif
