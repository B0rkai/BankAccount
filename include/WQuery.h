#pragma once
#include <set>
#include "Query.h"

class WQueryElement;
class Transaction;
class IWClient;
class IWCategory;
class IIdResolve;

class WQuery : public Query {
    WQueryElement* m_wqe = nullptr;
public:
    inline void AddWElement(WQueryElement* wqe) { m_wqe = wqe; }
    inline WQueryElement* WElement() { return m_wqe; }
};

// Query type which can modify the data
class WQueryElement {
protected:
    static const IIdResolve* s_resolve_if;
public:
    inline static void SetResolveIf(const IIdResolve* resif) { s_resolve_if = resif; }
    inline virtual bool ReadOnly() const { return false; }
    inline virtual QueryTopic GetTopic() const = 0;
    virtual bool CheckTransaction(Transaction* tr) = 0; // non-const parameter
    inline virtual void PreResolve() {};
    virtual void Execute(IWClient* client_if) {};
    virtual void Execute(IWCategory* client_if) {};
};

class ClientMergeQuery : public WQueryElement {
    uint16_t m_target_id = INVALID_CLIENT_ID;
    std::set<uint16_t> m_others;
    virtual bool IsOk() const;
    virtual void PreResolve();
    inline virtual QueryTopic GetTopic() const override { return QueryTopic::CLIENT; }
    virtual void Execute(IWClient* client_if) override;
    virtual bool CheckTransaction(Transaction* tr) override;
public:
    inline void AddTargetId(const uint16_t id) { m_target_id = id; }
    void AddOtherId(const uint16_t id);
};

class CategorizingQuery : public WQueryElement {
    inline virtual QueryTopic GetTopic() const override { return QueryTopic::CATEGORY; }
    virtual bool IsOk() const;
    inline virtual void Execute(IWCategory* category_if) override { if_category = category_if; };
    virtual bool CheckTransaction(Transaction* tr) override;
    bool TryCategorizing(Transaction* tr, std::string& text);
    uint8_t m_flags = 0u;
    IWCategory* if_category = nullptr;
public:
    enum Settings : uint8_t {
        CAUTIOUS = 1,
        MANUAL = 2,
        AUTOMATIC = 4,
        OVERRIDE = 8
    };
    inline void SetFlags(uint8_t flags) { m_flags = flags; };

};

