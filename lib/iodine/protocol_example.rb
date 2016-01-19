# The ProtocolExample class is used only for documenting the Protocol API, it will not be included when requiring `iodine`.
#
# A protocol class MUST contain ONE of the following callbacks:
#
# on_data:: called whened there's data available to be read, but no data was read just yet. `on_data` will not be called again untill all the existing network buffer was read (edge triggered event).
# on_message(buffer):: the default `on_data` implementation creates a 1Kb buffer and reads data while recycling the same String memory space. The buffer is forwarded to the `on_message` callback before being recycled. The buffer object will be over-written once `on_message` returns, so creating a persistent copy requires `buffer.dup`.
#
# A protocol class MAY contain any of the following optional callbacks:
#
# on_open:: called after a new connection was accepted and the protocol was linked with Iodine's Protocol API. Initialization should be performed here.
# ping:: called whenever timeout was reached. The default implementation will close the connection unless a protocol task ({Protocol#defer}, `on_data` or `on_message`) are busy in the background.
# on_shutdown:: called if the connection is still open while the server is shutting down. This allows the protocol to send a "going away" frame before the connection is closed and `on_close` is called.
# on_close:: called after a connection was closed, for any cleanup (if any).
#
# WARNING: for thread safety and connection management, `on_open`, `on_shutdown`, `on_close` and `ping` will all be performed within the reactor's main thread.
# Do not run long running tasks within these callbacks, or the server might block while you do.
# Use {#defer} to run protocol related tasks (this locks the connection, preventing it from running more then one task at a time and offering thread safety),
# or {#run} to run asynchronous tasks that aren't protocol related.
#
# API:
#
# After a new connection is accepted and a new protocol object is created, the protocol will be linked with Iodine's Protocol API.
# Only the main protocol will be able to access the API within `initialize`, so it's best to use `on_open` for any Initialization required.
#
# The ProtocolExample class contains method to document Iodine's protocol API (they all call `super`).
class ProtocolExample

  # Reads n bytes from the network connection, where n is:
  #
  # - the number of bytes set in the optional `buffer_or_length` argument.
  #
  # - the String capacity (not length) of the String passed as the optional `buffer_or_length` argument.
  #
  # - 1024 Bytes (1Kb) if the optional `buffer_or_length` is either missing or contains a String who's capacity is less then 1Kb.
  #
  # Always returns a String (either the same one used as the buffer or a new one). The string will be empty if no data was available.
  def read buffer_or_length = nil
    super
  end

  # Writes all the data in the String `data` to the connection.
  #
  # If the data cannot be written in a single non-blocking system call,
  # the data will be saved to an internal buffer and will be written asyncronously
  # whenever the socket signals that it's ready to write.
  def write data
    super
  end

  # Writes all the data in the String `data` to the connection.
  #
  # If the data cannot be written in a single non-blocking system call,
  # the data will be saved to an internal buffer and will be written asyncronously
  # whenever the socket signals that it's ready to write.
  #
  # If a write buffer already exists with multiple items in it's queue (package based protocol),
  # the data will be queued as the next "package" to be sent, so that packages aren't corrupted or injected in the middle of other packets.
  #
  # A "package" refers to a unit of data sent using {#write} and not to the TCP/IP packets.
  # This allows to easily write protocols that package their data, such as HTTP/2.
  #
  # {#write_urgent}'s best use-case is the `ping`, which shouldn't corrupt any data being senk, but must be sent as soon as possible.
  #
  # #{write}, {#write_urgent} and #{close} are designed to be thread safe.
  def write_urgent data
    super
  end

  # Closes the connection.
  #
  # If there is an internal write buffer with pending data to be sent (see {#write}),
  # {#close} will return immediately and the connection will only be closed when all the data was sent.
  def close
    super
  end

  # Closes the connection immediately, even if there's still data waiting to be sent (see {#close} and {#write}).
  def force_close
    super
  end

  # Replaces the current protocol instance with another.
  #
  # The `handler` is expected to be a protocol instance object.
  # Once it is passed to the {#upgrade} method, it's class will be extended to include
  # Iodine's API (by way of a mixin) and it's `on_open` callback will be called.
  #
  # If `handler` is a class, the API will be injected to the class (by way of a mixin)
  # and a new instance will be created before calling `on_open`.
  def upgrade handler
    super
  end

  # This schedules a task to be performed asynchronously within the lock of this
  # protocol's connection.
  #
  # The task won't be performed while `on_message` or `on_data` is still active.
  # No single connection will perform more then a single task at a time.
  def defer
    super
  end

  # This schedules a task to be performed asynchronously. In a multi-threaded
  # setting, tasks might be performed concurrently.
  def run
    super
  end
  # def run_after miliseconds
  #   super
  # end
  # def run_every miliseconds, repetitions
  #   super
  # end
end
