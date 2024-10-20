#pragma once
#include "Query.h"

class WQueryElement;
class Transaction;
class IIdResolve;
class IWClient;
class IWCategory;
class IWAccount;

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
    virtual void Execute(IWCategory* category_if) {};
    virtual void Execute(IWAccount* account_if) {};
};

class MergeQuery : public WQueryElement {
protected:
    Id m_target_id = INVALID_ID;
    IdSet m_others;
    virtual bool IsOk() const;
    virtual void PreResolve();
public:
    inline void AddTargetId(const uint16_t id) { m_target_id = id; }
    void AddOtherId(const uint16_t id);
};

class ClientMergeQuery : public MergeQuery {
    GETQUERYTOPIC(CLIENT)
    virtual void Execute(IWClient* client_if) override;
    virtual bool CheckTransaction(Transaction* tr) override;
};

class TypeMergeQuery : public MergeQuery {
    GETQUERYTOPIC(TYPE)
    virtual void Execute(IWAccount* account_if) override;
    virtual bool CheckTransaction(Transaction* tr) override;
};

class CategorizingQuery : public WQueryElement {
    GETQUERYTOPIC(CATEGORY)
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

