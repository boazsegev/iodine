---
name: Bug report
about: Create a report to help us improve
title: ''
labels: ''
assignees: ''

---

### System Information

 - **OS**:             [e.g. macOS 10.15.4]
 - **Ruby**:         [e.g. 2.7.0]
 - **Version**:     [e.g. 0.7.38]
 - **OpenSSL**:  [OpenSSL 1.1.1f 20 Mar 2020]

### Description

A clear and concise description of what the bug is.

### Rack App to Reproduce

```ruby
APP = {|env| [200, {}, "Hello World"] }
run APP
```

### Testing code

```sh
curl http://localhost:3000/
```

### Expected behavior

A clear and concise description of what you expected to happen.

### Actual behavior

A clear and concise description of what actually happened.
