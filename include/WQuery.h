#pragma once
#include "Query.h"
#include "Logger.h"

class WQueryElement;
class Transaction;
class IIdResolve;
class IWClient;
class IWCategorize;
class IWAccount;
class IManualResolve;

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
    WQueryElement() = default;
    virtual ~WQueryElement() = default;
    inline static void SetResolveIf(const IIdResolve* resif) { s_resolve_if = resif; }
    inline virtual bool ReadOnly() const { return false; }
    inline virtual String GetResult() const { return cStringEmpty; }
    inline virtual QueryTopic GetTopic() const = 0;
    virtual bool CheckTransaction(Transaction* tr) = 0; // non-const parameter
    inline virtual void PreResolve() {};
    virtual void Execute(IWAccount* account_if) = 0;
};

class MergeQuery : public WQueryElement {
protected:
    Id m_target_id = INVALID_ID;
    IdSet m_others;
    Logger& m_logger;
    virtual bool IsOk() const;
    virtual void PreResolve();
    virtual bool CheckTransaction(Transaction* tr) override;
    virtual void Execute(IWAccount* client_if) override;
public:
    MergeQuery();
    virtual ~MergeQuery() = default;
    inline void AddTargetId(const Id id) { m_target_id = id; }
    void AddOtherId(const Id id);
    void AddOtherIds(const IdSet ids);
};

class ClientMergeQuery : public MergeQuery {
    GETQUERYTOPIC(CLIENT)
};

class TypeMergeQuery : public MergeQuery {
    GETQUERYTOPIC(TYPE)
};

class CategoryMergeQuery : public MergeQuery {
    GETQUERYTOPIC(CATEGORY)
};

class CategorizingQuery : public WQueryElement {
    GETQUERYTOPIC(CATEGORY)
    virtual bool IsOk() const;
    virtual void Execute(IWAccount* account_if) override;
    virtual bool CheckTransaction(Transaction* tr) override;
    virtual String GetResult() const override;
    uint8_t m_flags = 0u;
    size_t m_all = 0;
    size_t m_did_not_change = 0;
    size_t m_no_category_found = 0;
    size_t m_automatic_categorized = 0;
    size_t m_manual_categorized = 0;
    IWCategorize* if_categorize = nullptr;
    IManualResolve* if_manual_resolve = nullptr;
public:
    enum Settings : uint8_t {
        CAUTIOUS = 1,
        MANUAL = 2,
        AUTOMATIC = 4,
        OVERRIDE = 8
    };
    inline void SetManualResolveIf(IManualResolve* resolver_if) { if_manual_resolve = resolver_if; }
    inline void SetFlags(uint8_t flags) { m_flags = flags; };

};

