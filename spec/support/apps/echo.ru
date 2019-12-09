run ->(env) do
   body = env['rack.input'].read
   if body.length > 0
     puts "BODY(#{body.length}): #{body[0...8]}"
   else
     puts "NO BODY (content-length: #{env['CONTENT_LENGTH'].to_s})"
   end
   [200, {}, [body] ]
end
