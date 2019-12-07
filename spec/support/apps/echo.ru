run ->(env) do
   body = env['rack.input'].read

   [200, {}, [body] ]
end
