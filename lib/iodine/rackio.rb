require 'iodine'
require 'iodine/iodine_http'


class Iodine
  class Http
    # RackIO is the IO gateway for the HTTP request's body.
    #
    # creating a custom IO class allows Rack to directly access the C data and minimizes data duplication, enhancing performance.
    class RackIO
    end
  end
end
