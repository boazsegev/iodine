module Iodine
   # Iodine is equiped with an internal pub/sub service that allows improved resource management from a deployment perspective.
   #
   # As described by [Wikipedia](https://en.wikipedia.org/wiki/Publish–subscribe_pattern):
   #
   #    publish–subscribe is a messaging pattern where senders of messages, called publishers, do not program the messages to be sent directly to specific receivers, called subscribers, but instead characterize published messages into classes without knowledge of which subscribers, if any, there may be. Similarly, subscribers express interest in one or more classes and only receive messages that are of interest, without knowledge of which publishers, if any, there are.
   #
   # The common paradigm, which is implemented by pub/sub services like Redis,
   # is for a "client" to "subscribe" to one or more "channels". Messages are streamed
   # to these "channels" by different "publishers" (the application / other clients) and are
   # broadcasted to the "clients" through their "subscription".
   #
   # Iodine's approach it to offload pub/sub resource costs from the pub/sub service
   # (which is usually expensive to scale) onto the application layer.
   #
   # For example, the default (`nil`) pub/sub {Iodine::PubSub::Engine} implements
   # an internal pub/sub service that manages subscriptions (clients and channels) throughout an Iodine process cluster without any need to connect to an external pub/sub service.
   #
   # If Iodine was runninng with 8 processes and 16 threads per process,
   # a publishing in process A will be delivered to subscribers in process B.
   #
   # In addition, by inheriting the {Iodine::PubSub::Engine} class, it's easy to create pub/sub engines that connect to this
   # underlying pub/sub service. This means that Iodine will call the engine's `subscribe` method only once per
   # channel and once messages arrive, Iodine will distribute the messages to all the subscribed clients.
  module PubSub
    # The {Iodine::PubSub::Engine} class makes it easy to use leverage Iodine's pub/sub system using external services.
    #
    # Iodine comes with two built-in engines:
    #
    # * `Iodine::PubSub::Engine::CLUSTER` will distribute messages to all subscribers in the process cluster.
    # * `Iodine::PubSub::Engine::SINGLE_PROCESS` will distribute messages to all subscribers sharing the same process.
    #
    # {Iodine::PubSub::Engine} instances should be initialized only after Iodine
    # started running (or the `fork`ing of the engine's connection will introduce communication issues).
    #
    # For this reason, the best approcah to initialization would be:
    #
    #       class MyEngineClass < Iodine::PubSub::Engine
    #            # ...
    #       end
    #
    #       Iodine.run do
    #          MyEngine = MyEngineClass.new
    #       end
    #
    # {Iodine::PubSub::Engine} child classes MUST override the {Iodine::PubSub::Engine#subscribe},
    # {Iodine::PubSub::Engine#unsubscribe} and {Iodine::PubSub::Engine#publish}
    # in order to perform this actions using the backend service (i.e. using Redis).
    #
    # Once an {Iodine::PubSub::Engine} instance receives a message from the backend service,
    # it should forward the message to the Iodine distribution layer using the {Iodine::PubSub::Engine#distribute} method.
    #
    # Iodine will than distribute the message to all registered clients.
    #
    # *IMPORTANT*:
    #
    # Connections shouldn't call any of the {Iodine::PubSub::Engine} or {Iodine::PubSub} methods directly,
    # since connections might be disconnected at any time, at which point their memory will become corrupted or recycled.
    #
    # The connection pub/sub methods (coming soon) keep the application safe from these
    # risks by performing specific checks for connection related pub/sub actions.
    #
    class Engine
    end
    # This is the (currently) default pub/sub engine. It will distribute messages to all subscribers in the process cluster.
    CLUSTER
    # This is a single process pub/sub engine. It will distribute messages to all subscribers sharing the same process.
    SINGLE_PROCESS
  end
end
