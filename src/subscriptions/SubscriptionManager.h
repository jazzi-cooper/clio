#ifndef SUBSCRIPTION_MANAGER_H
#define SUBSCRIPTION_MANAGER_H

#include <backend/BackendInterface.h>
#include <memory>
#include <subscriptions/Message.h>

class WsBase;

class Subscription
{
    boost::asio::io_context::strand strand_;
    std::unordered_set<std::shared_ptr<WsBase>> subscribers_ = {};

public:
    Subscription() = delete;
    Subscription(Subscription&) = delete;
    Subscription(Subscription&&) = delete;

    explicit Subscription(boost::asio::io_context& ioc) : strand_(ioc)
    {
    }

    ~Subscription() = default;

    void
    subscribe(std::shared_ptr<WsBase> const& session);

    void
    unsubscribe(std::shared_ptr<WsBase> const& session);

    void
    publish(std::shared_ptr<Message>& message);
};

template <class Key>
class SubscriptionMap
{
    using subscribers = std::unordered_set<std::shared_ptr<WsBase>>;

    boost::asio::io_context::strand strand_;
    std::unordered_map<Key, subscribers> subscribers_ = {};

public:
    SubscriptionMap() = delete;
    SubscriptionMap(SubscriptionMap&) = delete;
    SubscriptionMap(SubscriptionMap&&) = delete;

    explicit SubscriptionMap(boost::asio::io_context& ioc) : strand_(ioc)
    {
    }

    ~SubscriptionMap() = default;

    void
    subscribe(std::shared_ptr<WsBase> const& session, Key const& key);

    void
    unsubscribe(std::shared_ptr<WsBase> const& session, Key const& key);

    void
    publish(std::shared_ptr<Message>& message, Key const& key);
};

class SubscriptionManager
{
    std::vector<std::thread> workers_;
    boost::asio::io_context ioc_;
    std::optional<boost::asio::io_context::work> work_;

    Subscription ledgerSubscribers_;
    Subscription txSubscribers_;
    Subscription txProposedSubscribers_;
    Subscription manifestSubscribers_;
    Subscription validationsSubscribers_;

    SubscriptionMap<ripple::AccountID> accountSubscribers_;
    SubscriptionMap<ripple::AccountID> accountProposedSubscribers_;
    SubscriptionMap<ripple::Book> bookSubscribers_;

    std::shared_ptr<Backend::BackendInterface const> backend_;

public:
    static std::shared_ptr<SubscriptionManager>
    make_SubscriptionManager(
        boost::json::object const& config,
        std::shared_ptr<Backend::BackendInterface const> const& b)
    {
        auto numThreads = 1;

        if (config.contains("subscription_workers") &&
            config.at("subscription_workers").is_int64())
        {
            numThreads = config.at("subscription_workers").as_int64();
        }

        return std::make_shared<SubscriptionManager>(numThreads, b);
    }

    SubscriptionManager(
        std::uint64_t numThreads,
        std::shared_ptr<Backend::BackendInterface const> const& b)
        : ledgerSubscribers_(ioc_)
        , txSubscribers_(ioc_)
        , txProposedSubscribers_(ioc_)
        , manifestSubscribers_(ioc_)
        , validationsSubscribers_(ioc_)
        , accountSubscribers_(ioc_)
        , accountProposedSubscribers_(ioc_)
        , bookSubscribers_(ioc_)
        , backend_(b)
    {
        work_.emplace(ioc_);

        // We will eventually want to clamp this to be the number of strands,
        // since adding more threads than we have strands won't see any
        // performance benefits
        BOOST_LOG_TRIVIAL(info) << "Starting subscription manager with "
                                << numThreads << " workers";

        workers_.reserve(numThreads);
        for (auto i = numThreads; i > 0; --i)
            workers_.emplace_back([this] { ioc_.run(); });
    }

    ~SubscriptionManager()
    {
        work_.reset();

        ioc_.stop();
        for (auto& worker : workers_)
            worker.join();
    }

    boost::json::object
    subLedger(
        boost::asio::yield_context& yield,
        std::shared_ptr<WsBase>& session);

    void
    pubLedger(
        ripple::LedgerInfo const& lgrInfo,
        ripple::Fees const& fees,
        std::string const& ledgerRange,
        std::uint32_t txnCount);

    void
    unsubLedger(std::shared_ptr<WsBase>& session);

    void
    subTransactions(std::shared_ptr<WsBase>& session);

    void
    unsubTransactions(std::shared_ptr<WsBase>& session);

    void
    pubTransaction(
        Backend::TransactionAndMetadata const& blobs,
        ripple::LedgerInfo const& lgrInfo);

    void
    subAccount(
        ripple::AccountID const& account,
        std::shared_ptr<WsBase>& session);

    void
    unsubAccount(
        ripple::AccountID const& account,
        std::shared_ptr<WsBase>& session);

    void
    subBook(ripple::Book const& book, std::shared_ptr<WsBase>& session);

    void
    unsubBook(ripple::Book const& book, std::shared_ptr<WsBase>& session);

    void
    subManifest(std::shared_ptr<WsBase>& session);

    void
    unsubManifest(std::shared_ptr<WsBase>& session);

    void
    subValidation(std::shared_ptr<WsBase>& session);

    void
    unsubValidation(std::shared_ptr<WsBase>& session);

    void
    forwardProposedTransaction(boost::json::object const& response);

    void
    forwardManifest(boost::json::object const& response);

    void
    forwardValidation(boost::json::object const& response);

    void
    subProposedAccount(
        ripple::AccountID const& account,
        std::shared_ptr<WsBase>& session);

    void
    unsubProposedAccount(
        ripple::AccountID const& account,
        std::shared_ptr<WsBase>& session);

    void
    subProposedTransactions(std::shared_ptr<WsBase>& session);

    void
    unsubProposedTransactions(std::shared_ptr<WsBase>& session);

private:
    void
    sendAll(
        std::string const& pubMsg,
        std::unordered_set<std::shared_ptr<WsBase>>& subs);
};

#endif  // SUBSCRIPTION_MANAGER_H
