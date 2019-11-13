---
name: Bug report
about: Create a report to help us improve
title: ''
labels: ''
assignees: ''

---

### System Information

 - **OS**:             [e.g. macOS 10.14.6]
 - **Ruby**:         [e.g. 2.6.1]
 - **Version**:     [e.g. 0.7.37]
 - **OpenSSL**:  [OpenSSL 1.1.1d 10 Sep 2019]

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

**Smartphone (please complete the following information):**
 - Device: [e.g. iPhone6]
 - OS: [e.g. iOS8.1]
 - Browser [e.g. stock browser, safari]
 - Version [e.g. 22]

**Additional context**
Add any other context about the problem here.
