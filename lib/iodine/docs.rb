# Please read the {file:README.md} file for an introduction to Iodine and an overview of it's API.
class Iodine
  # This method accepts a block and sets the block to run immediately after the server had started.
  #
  # Since functions such as `run`, `run_every` and `run_after` can only be called AFTER the reactor had started,
  # use {#on_start} to setup any global tasks. i.e.
  #
  #     server = Iodine.new
  #     server.on_start do
  #         puts "My network service had started."
  #         server.run_every(5000) {puts "#{server.count -1 } clients connected."}
  #     end
  #     # ...
  #
  def on_start
  end
  # starts the service.
  def start
  end
  # Runs an asynchronous task.
  def run
  end
  # Runs an asynchronous task after set amount of milliseconds.
  def run_after milliseconds
  end
  # Runs an asynchronous task every set amount of milliseconds. see {DynProtocol#run_every}.
  def run_every milliseconds
  end
  # returns the total number of connections and timers currently used.
  # Timers ARE connections in their implementation and aren't counted seperately by Iodine.
  def count
  end
end
