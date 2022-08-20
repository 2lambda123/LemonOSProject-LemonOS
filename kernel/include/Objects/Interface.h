#pragma once

#include <Objects/KObject.h>
#include <Objects/Message.h>

#include <List.h>

class MessageInterface final : public KernelObject{
    DECLARE_KOBJECT(MessageInterface);
protected:
    bool active = true;

    char* name;
    List<FancyRefPtr<MessageEndpoint>> connections;

    uint16_t msgSize;
    lock_t incomingLock = 0;
    List<Pair<int, FancyRefPtr<MessageEndpoint>>*> incoming;

    lock_t waitingLock = 0;
    List<KernelObjectWatcher*> waiting;

    friend class Service;
public:
    MessageInterface(const char* _name, uint16_t msgSize);
    ~MessageInterface();

    void Destroy();

    /////////////////////////////
    /// \brief Accept an incoming connection
    ///
    /// Creates a message channel and populateds endpoint with a MessageEndpoint
    ///
    /// \return 0 on success, 1 when no incoming connections, negative error code on failure
    /////////////////////////////
    long Accept(FancyRefPtr<MessageEndpoint>& endpoint);

    /////////////////////////////
    /// \brief Connect to interface
    ///
    /// Initiates a connection, blocks until it is accepted by the interface owner and returns a message endpoint. Should never fail.
    ///
    /// \return Pointer to MessageEndpoint
    /////////////////////////////
    FancyRefPtr<MessageEndpoint> Connect();

    void Watch(KernelObjectWatcher& watcher, int events) override {
        acquireLock(&waitingLock);
        waiting.add_back(&watcher);
        releaseLock(&waitingLock)
    }

    virtual void Unwatch(KernelObjectWatcher& watcher) override {
        waiting.remove(&watcher);
    }
};
