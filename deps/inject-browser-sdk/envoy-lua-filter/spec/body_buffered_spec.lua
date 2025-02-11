require 'busted.runner' ()

local DatadogRUM
local internals
local ResponseHandle = require('spec.__mocks__.response')

describe('Handling the body', function()
  local enabled_headers
  local enabled_metadata

  before_each(function()
    _G._TEST = true
    DatadogRUM = require('src.datadog_rum')
    internals = DatadogRUM.__internals
    enabled_headers = { ['content-type'] = internals.INJECTED_MIMETYPE }
    enabled_metadata = { [internals.CONFIG_KEY] = { enabled = true, stream_response = false } }
  end)

  teardown(function()
    _G._TEST = nil
  end)

  describe('when the response is buffered', function()
    it('should not remove the content-length header when not injecting', function()
      local response = ResponseHandle.new(enabled_headers, enabled_metadata, '', true)
      local removeHeaders = spy.on(response:headers(), 'remove')

      DatadogRUM.inject_response(response)

      assert.spy(removeHeaders).was.not_called(match._, 'content-length')
    end)

    it('should modify the content-length header when injecting', function()
      local tag = internals.build_tag()
      local response = ResponseHandle.new(enabled_headers, enabled_metadata, '</head>', true)
      local replaceHeaders = spy.on(response:headers(), 'replace')

      DatadogRUM.inject_response(response)

      assert.spy(replaceHeaders).was.called_with(match._, 'content-length', #(tag) + #('</head>'))
    end)

    it('should read the entire body length', function()
      local response = ResponseHandle.new(enabled_headers, enabled_metadata, '</head>', true)
      local bodyGetBytes = spy.on(response:body(), 'getBytes')

      DatadogRUM.inject_response(response)

      assert.spy(bodyGetBytes).was.called(1)
      assert.spy(bodyGetBytes).was.called_with(match._, 0, #('</head>'))
    end)

    it('should inject once if specified', function()
      enabled_metadata[internals.CONFIG_KEY].inject_times = 3
      local tag = internals.build_tag()
      local response = ResponseHandle.new(enabled_headers, enabled_metadata, '</head>', true)
      local bodySetbytes = spy.on(response:body(), 'setBytes')
      local replaceHeaders = spy.on(response:headers(), 'replace')

      DatadogRUM.inject_response(response)

      local result = tag .. '</head>'
      assert.spy(bodySetbytes).was.called_with(match._, result)
      assert.spy(replaceHeaders).was.called_with(match._, 'content-length', #(result))
    end)

    it('should inject twice if specified', function()
      enabled_metadata[internals.CONFIG_KEY].inject_times = 2
      local tag = internals.build_tag()
      local response = ResponseHandle.new(enabled_headers, enabled_metadata, '</head></head></head>', true)
      local bodySetbytes = spy.on(response:body(), 'setBytes')
      local replaceHeaders = spy.on(response:headers(), 'replace')

      DatadogRUM.inject_response(response)

      local result = string.rep(tag .. '</head>', 2) .. '</head>'
      assert.spy(bodySetbytes).was.called_with(match._, result)
      assert.spy(replaceHeaders).was.called_with(match._, 'content-length', #(result))
    end)

    it('should inject three times if specified', function()
      enabled_metadata[internals.CONFIG_KEY].inject_times = 3
      local tag = internals.build_tag()
      local response = ResponseHandle.new(enabled_headers, enabled_metadata, '</head></head></head>', true)
      local bodySetbytes = spy.on(response:body(), 'setBytes')
      local replaceHeaders = spy.on(response:headers(), 'replace')

      DatadogRUM.inject_response(response)

      local result = string.rep(tag .. '</head>', 3)
      assert.spy(bodySetbytes).was.called_with(match._, result)
      assert.spy(replaceHeaders).was.called_with(match._, 'content-length', #(result))
    end)

    it('should inject before the desired match', function()
      local tag = internals.build_tag()
      enabled_metadata[internals.CONFIG_KEY].inject_before = 'bar'
      local response = ResponseHandle.new(enabled_headers, enabled_metadata, 'foobarbaz', true)
      local bodySetbytes = spy.on(response:body(), 'setBytes')
      local replaceHeaders = spy.on(response:headers(), 'replace')

      DatadogRUM.inject_response(response)

      local result = 'foo' .. tag .. 'barbaz'
      assert.spy(bodySetbytes).was.called_with(match._, result)
      assert.spy(replaceHeaders).was.called_with(match._, 'content-length', #(result))
    end)
  end)
end)
