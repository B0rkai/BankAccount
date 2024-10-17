#pragma once
#include <set>
#include "Query.h"

class WQueryElement;
class Transaction;
class IWClient;

class WQuery : public Query {
    WQueryElement* m_wqe = nullptr;
public:
    inline void AddWElement(WQueryElement* wqe) { m_wqe = wqe; }
    inline WQueryElement* WElement() { return m_wqe; }
};

// Query type which can modify the data
class WQueryElement {
public:
    inline virtual bool ReadOnly() const { return false; }
    inline virtual QueryTopic GetTopic() const = 0;
    virtual bool CheckTransaction(Transaction* tr) = 0; // non-const parameter
    inline virtual void PreResolve() {};
    virtual void Execute(IWClient* client_if) {};
};

class ClientMergeQuery : public WQueryElement {
    uint16_t m_target_id = INVALID_CLIENT_ID;
    std::set<uint16_t> m_others;
    virtual bool IsOk() const;
    virtual void PreResolve();
    inline virtual QueryTopic GetTopic() const override { return CLIENT; }
    virtual void Execute(IWClient* client_if) override;
public:
    inline void AddTargetId(const uint16_t id) { m_target_id = id; }
    void AddOtherId(const uint16_t id);
    virtual bool CheckTransaction(Transaction* tr) override;
};

