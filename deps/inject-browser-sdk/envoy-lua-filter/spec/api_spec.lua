require 'busted.runner' ()

local DatadogRUM
local internals
local RequestHandle  = require('spec.__mocks__.request')
local ResponseHandle = require('spec.__mocks__.response')

describe('The public API', function()
  before_each(function()
    _G._TEST = true
    DatadogRUM = require('src.datadog_rum')
    internals = DatadogRUM.__internals
  end)

  teardown(function() _G._TEST = nil end)

  describe('on requests', function()
    it('should imprint a pristine request', function()
      local request = RequestHandle.new()

      DatadogRUM.imprint_request(request)

      assert.are.equal(request:headers():get(internals.PENDING_HEADER_NAME), '1')
    end)

    it('should not imprint a pending request', function()
      local request = RequestHandle.new({ [internals.PENDING_HEADER_NAME] = '1' })
      local addHeaders = spy.on(request:headers(), 'add')

      DatadogRUM.imprint_request(request)

      assert.spy(addHeaders).was.called(0)
    end)
  end)

  describe('on responses', function()
    local enabled_metadata
    before_each(function()
      enabled_metadata = { [internals.CONFIG_KEY] = { enabled = true, stream_response = false } }
    end)

    it('should not handle a response with no config', function()
      local response = ResponseHandle.new()
      local getHeaders = spy.on(response:headers(), 'get')
      local getMetadata = spy.on(response:metadata(), 'get')

      DatadogRUM.inject_response(response)

      assert.spy(getMetadata).was.called(1)
      assert.spy(getMetadata).was.called_with(match._, internals.CONFIG_KEY)
      assert.is_nil(response:metadata():get(internals.CONFIG_KEY))
      assert.spy(getHeaders).was.called(0) -- lack of content-type read as a sign of no-op
    end)

    it('should not handle a response with disabled config', function()
      local response = ResponseHandle.new(nil, { [internals.CONFIG_KEY] = { enabled = false } })
      local getHeaders = spy.on(response:headers(), 'get')

      DatadogRUM.inject_response(response)

      assert.is_false(response:metadata():get(internals.CONFIG_KEY)['enabled'])
      assert.spy(getHeaders).was.called(0) -- lack of content-type read as a sign of no-op
    end)

    it('should handle a response with enabled config', function()
      local response = ResponseHandle.new(nil, enabled_metadata, '', true)
      local getHeaders = spy.on(response:headers(), 'get')
      local getBodyBytes = spy.on(response:body(), 'getBytes')

      DatadogRUM.inject_response(response)

      assert.is_true(response:metadata():get(internals.CONFIG_KEY)['enabled'])
      assert.spy(getHeaders).was.called_with(match._, 'content-type')
      assert.spy(getBodyBytes).was.called(0) -- lack of body read as a sign of no-op
    end)

    it('should handle a response with enabled config on right mime type', function()
      local response = ResponseHandle.new(
        { ['content-type'] = internals.INJECTED_MIMETYPE },
        enabled_metadata,
        '', true
      )
      local getHeaders = spy.on(response:headers(), 'get')
      local addHeaders = spy.on(response:headers(), 'add')
      local getBodyBytes = spy.on(response:body(), 'getBytes')

      DatadogRUM.inject_response(response)

      assert.spy(getHeaders).was.called(2)
      assert.spy(getHeaders).was.called_with(match._, 'content-type')
      assert.spy(addHeaders).was.called_with(match._, internals.INJECTED_HEADER_NAME, '1')
      assert.spy(getBodyBytes).was.called(1) -- body read as a sign of operation
    end)

    it('should handle a response with enabled config on right mime type with parameters', function()
      local response = ResponseHandle.new(
        { ['content-type'] = internals.INJECTED_MIMETYPE .. '; charset=UTF-8' },
        enabled_metadata,
        '', true
      )
      local getHeaders = spy.on(response:headers(), 'get')
      local addHeaders = spy.on(response:headers(), 'add')
      local getBodyBytes = spy.on(response:body(), 'getBytes')

      DatadogRUM.inject_response(response)

      assert.spy(getHeaders).was.called(2)
      assert.spy(getHeaders).was.called_with(match._, 'content-type')
      assert.spy(addHeaders).was.called_with(match._, internals.INJECTED_HEADER_NAME, '1')
      assert.spy(getBodyBytes).was.called(1) -- body read as a sign of operation
    end)

    it('should not handle a response with enabled config on wrong mime type', function()
      local response = ResponseHandle.new(
        { ['content-type'] = 'application/json' },
        enabled_metadata,
        '', true
      )
      local getHeaders = spy.on(response:headers(), 'get')
      local addHeaders = spy.on(response:headers(), 'add')
      local getBodyBytes = spy.on(response:body(), 'getBytes')

      DatadogRUM.inject_response(response)

      assert.spy(getHeaders).was.called_with(match._, 'content-type')
      assert.spy(addHeaders).was.not_called_with(match._, internals.INJECTED_HEADER_NAME, '1')
      assert.spy(getBodyBytes).was.called(0) -- lack of body read as a sign of no-op
    end)

    it('should not handle a response with enabled config on absent mime type', function()
      local response = ResponseHandle.new(nil, enabled_metadata, '', true)
      local getHeaders = spy.on(response:headers(), 'get')
      local addHeaders = spy.on(response:headers(), 'add')
      local getBodyBytes = spy.on(response:body(), 'getBytes')

      DatadogRUM.inject_response(response)

      assert.spy(getHeaders).was.called_with(match._, 'content-type')
      assert.spy(addHeaders).was.not_called_with(match._, internals.INJECTED_HEADER_NAME, '1')
      assert.spy(getBodyBytes).was.called(0) -- lack of body read as a sign of no-op
    end)

    it('should not handle a response that was already injected', function()
      local response = ResponseHandle.new(
        { ['content-type'] = internals.INJECTED_MIMETYPE, [internals.INJECTED_HEADER_NAME] = '1' },
        enabled_metadata,
        '', true
      )
      local getHeaders = spy.on(response:headers(), 'get')
      local addHeaders = spy.on(response:headers(), 'add')
      local getBodyBytes = spy.on(response:body(), 'getBytes')

      DatadogRUM.inject_response(response)

      assert.spy(getHeaders).was.called_with(match._, 'content-type')
      assert.spy(getHeaders).was.called_with(match._, internals.INJECTED_HEADER_NAME)
      assert.spy(addHeaders).was.not_called_with(match._, internals.INJECTED_HEADER_NAME, '1')
      assert.spy(getBodyBytes).was.called(0) -- lack of body read as a sign of no-op
    end)

    describe('when having csp additions', function()
      before_each(function()
        enabled_metadata = { [internals.CONFIG_KEY] = { enabled = true, stream_response = false } }
      end)

      it('should not set the content-security-header if not injected on buffered responses', function()
        enabled_metadata[internals.CONFIG_KEY]['stream_response'] = false
        enabled_metadata[internals.CONFIG_KEY]['csp_additions'] = 'worker-src: blob:;'
        local response = ResponseHandle.new(
          { ['content-type'] = internals.INJECTED_MIMETYPE .. '; charset=UTF-8' },
          enabled_metadata,
          'foo', true
        )
        local getHeaders = spy.on(response:headers(), 'get')
        local addHeaders = spy.on(response:headers(), 'add')
        local replaceHeaders = spy.on(response:headers(), 'replace')
        local getBodyBytes = spy.on(response:body(), 'getBytes')

        DatadogRUM.inject_response(response)

        assert.spy(getHeaders).was.called(2)
        assert.spy(getHeaders).was.called_with(match._, 'content-type')
        assert.spy(addHeaders).was.called_with(match._, internals.INJECTED_HEADER_NAME, '1')
        assert.spy(getHeaders).was.not_called_with(match._, 'content-security-policy')
        assert.spy(replaceHeaders).was.not_called_with(match._, 'content-security-policy', match.is_string())
        assert.spy(getBodyBytes).was.called(1) -- body read as a sign of operation
      end)

      it('should set the content-security-header if injected on buffered responses', function()
        enabled_metadata[internals.CONFIG_KEY]['stream_response'] = false
        enabled_metadata[internals.CONFIG_KEY]['csp_additions'] = 'worker-src: blob:;'
        local response = ResponseHandle.new(
          { ['content-type'] = internals.INJECTED_MIMETYPE .. '; charset=UTF-8' },
          enabled_metadata,
          '</head>', true
        )
        local getHeaders = spy.on(response:headers(), 'get')
        local addHeaders = spy.on(response:headers(), 'add')
        local replaceHeaders = spy.on(response:headers(), 'replace')
        local getBodyBytes = spy.on(response:body(), 'getBytes')

        DatadogRUM.inject_response(response)

        assert.spy(getHeaders).was.called(3)
        assert.spy(getHeaders).was.called_with(match._, 'content-type')
        assert.spy(addHeaders).was.called_with(match._, internals.INJECTED_HEADER_NAME, '1')
        assert.spy(getHeaders).was.called_with(match._, 'content-security-policy')
        assert.spy(replaceHeaders).was.called_with(match._, 'content-security-policy', match._)
        assert.spy(getBodyBytes).was.called(1) -- body read as a sign of operation
      end)

      it('should not set CSP additions if not specified when using transfer-encoding: chunked', function()
        enabled_metadata[internals.CONFIG_KEY]['stream_response'] = true
        local response = ResponseHandle.new(
          { ['content-type'] = internals.INJECTED_MIMETYPE .. '; charset=UTF-8' },
          enabled_metadata,
          '', false
        )
        local getHeaders = spy.on(response:headers(), 'get')
        local addHeaders = spy.on(response:headers(), 'add')
        local replaceHeaders = spy.on(response:headers(), 'replace')
        local getBodyChunks = spy.on(response:body(), 'bodyChunks')

        DatadogRUM.inject_response(response)

        assert.spy(getHeaders).was.called(2)
        assert.spy(getHeaders).was.called_with(match._, 'content-type')
        assert.spy(addHeaders).was.called_with(match._, internals.INJECTED_HEADER_NAME, '1')
        assert.spy(getHeaders).was.not_called_with(match._, 'content-security-policy')
        assert.spy(replaceHeaders).was.not_called_with(match._, 'content-security-policy', match.is_string())
        assert.spy(getBodyChunks).was.called(1) -- body read as a sign of operation
      end)

      it('should preemptively set CSP additions when using transfer-encoding: chunked', function()
        enabled_metadata[internals.CONFIG_KEY]['stream_response'] = true
        enabled_metadata[internals.CONFIG_KEY]['csp_additions'] = 'worker-src: blob:;'
        local response = ResponseHandle.new(
          { ['content-type'] = internals.INJECTED_MIMETYPE .. '; charset=UTF-8' },
          enabled_metadata,
          '', false
        )
        local getHeaders = spy.on(response:headers(), 'get')
        local addHeaders = spy.on(response:headers(), 'add')
        local replaceHeaders = spy.on(response:headers(), 'replace')
        local getBodyChunks = spy.on(response:body(), 'bodyChunks')

        DatadogRUM.inject_response(response)

        assert.spy(getHeaders).was.called(3)
        assert.spy(getHeaders).was.called_with(match._, 'content-type')
        assert.spy(addHeaders).was.called_with(match._, internals.INJECTED_HEADER_NAME, '1')
        assert.spy(getHeaders).was.called_with(match._, 'content-security-policy')
        assert.spy(replaceHeaders).was.called_with(match._, 'content-security-policy', match._)
        assert.spy(getBodyChunks).was.called(1) -- body read as a sign of operation
      end)
    end)
  end)
end)
