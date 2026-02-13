require 'busted.runner' ()

local DatadogRUM
local internals
local ResponseHandle       = require('spec.__mocks__.response')
local ResponseBodyBuffered = require('spec.__mocks__.response_body_buffered')

describe('Handling the body', function()
  local enabled_headers
  local enabled_metadata

  teardown(function()
    _G._TEST = nil
  end)

  describe('when the response is chunked', function()
    before_each(function()
      _G._TEST = true
      DatadogRUM = require('src.datadog_rum')
      internals = DatadogRUM.__internals
      enabled_headers = { ['content-type'] = internals.INJECTED_MIMETYPE }
      enabled_metadata = { [internals.CONFIG_KEY] = { enabled = true, stream_response = true } }
    end)

    it('should remove the content-length header', function()
      local response = ResponseHandle.new(enabled_headers, enabled_metadata)
      local removeHeaders = spy.on(response:headers(), 'remove')

      DatadogRUM.inject_response(response)

      assert.spy(removeHeaders).was.called_with(match._, 'content-length')
    end)

    it('should read all chunks', function()
      local chunks = { ResponseBodyBuffered.new('chunk1'), ResponseBodyBuffered.new('chunk2') }
      local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)
      local c1GetBytes = spy.on(chunks[1], 'getBytes')
      local c2GetBytes = spy.on(chunks[2], 'getBytes')

      DatadogRUM.inject_response(response)

      assert.spy(c1GetBytes).was.called(1)
      assert.spy(c2GetBytes).was.called(1)
    end)

    it('should continue if a chunk is empty', function()
      local chunks = { ResponseBodyBuffered.new(''), ResponseBodyBuffered.new('chunk2') }
      local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)
      local c1GetBytes = spy.on(chunks[1], 'getBytes')
      local c2GetBytes = spy.on(chunks[2], 'getBytes')

      DatadogRUM.inject_response(response)

      assert.spy(c1GetBytes).was.called(0)
      assert.spy(c2GetBytes).was.called(1)
    end)

    describe('with a single buffer', function()
      before_each(function()
        _G._TEST = true
        DatadogRUM = require('src.datadog_rum')
        internals = DatadogRUM.__internals
        enabled_headers = { ['content-type'] = internals.INJECTED_MIMETYPE }
        enabled_metadata = { [internals.CONFIG_KEY] = { enabled = true, stream_response = true } }
      end)

      it('should not inject if not matched', function()
        local chunks = { ResponseBodyBuffered.new('<body>') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)

        DatadogRUM.inject_response(response)

        assert.is.equal('<body>', chunks[1]:getBytes(0, chunks[1]:length()))
      end)

      it('should inject once if specified', function()
        local tag = internals.build_tag()
        enabled_metadata[internals.CONFIG_KEY].inject_times = 1
        local chunks = { ResponseBodyBuffered.new('</head>') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)

        DatadogRUM.inject_response(response)

        assert.is.equal(tag .. '</head>', chunks[1]:getBytes(0, chunks[1]:length()))
      end)

      it('should inject twice if specified', function()
        local tag = internals.build_tag()
        enabled_metadata[internals.CONFIG_KEY].inject_times = 2
        local chunks = { ResponseBodyBuffered.new('</head></head></head>') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)

        DatadogRUM.inject_response(response)

        assert.is.equal(tag .. '</head>' .. tag .. '</head></head>', chunks[1]:getBytes(0, chunks[1]:length()))
      end)

      it('should inject three times if specified', function()
        local tag = internals.build_tag()
        enabled_metadata[internals.CONFIG_KEY].inject_times = 3
        local chunks = { ResponseBodyBuffered.new('</head></head></head>') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)

        DatadogRUM.inject_response(response)

        assert.is.equal(string.rep(tag .. '</head>', 3), chunks[1]:getBytes(0, chunks[1]:length()))
      end)

      it('should inject before the desired match', function()
        local tag = internals.build_tag()
        enabled_metadata[internals.CONFIG_KEY].inject_before = 'bar'
        local chunks = { ResponseBodyBuffered.new('foobarbaz') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)

        DatadogRUM.inject_response(response)

        assert.is.equal('foo' .. tag .. 'barbaz', chunks[1]:getBytes(0, chunks[1]:length()))
      end)
    end)

    describe('with multiple buffers', function()
      before_each(function()
        _G._TEST = true
        DatadogRUM = require('src.datadog_rum')
        internals = DatadogRUM.__internals
        enabled_headers = { ['content-type'] = internals.INJECTED_MIMETYPE }
        enabled_metadata = { [internals.CONFIG_KEY] = { enabled = true, stream_response = true } }
      end)

      it('should read the entire buffer length', function()
        local chunks = { ResponseBodyBuffered.new('<body>'), ResponseBodyBuffered.new('</body>') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)
        local c1GetBytes = spy.on(chunks[1], 'getBytes')
        local c2GetBytes = spy.on(chunks[2], 'getBytes')

        DatadogRUM.inject_response(response)

        assert.spy(c1GetBytes).was.called(1)
        assert.spy(c1GetBytes).was.called_with(match._, 0, chunks[1]:length())
        assert.spy(c2GetBytes).was.called(1)
        assert.spy(c2GetBytes).was.called_with(match._, 0, chunks[2]:length())
        assert.is.equal('<body>', chunks[1]:getBytes(0, chunks[1]:length()))
        assert.is.equal('</body>', chunks[2]:getBytes(0, chunks[2]:length()))
      end)

      it('should not inject if not matched', function()
        local chunks = { ResponseBodyBuffered.new('<body>'), ResponseBodyBuffered.new('</body>') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)

        DatadogRUM.inject_response(response)

        assert.is.equal('<body>', chunks[1]:getBytes(0, chunks[1]:length()))
        assert.is.equal('</body>', chunks[2]:getBytes(0, chunks[2]:length()))
      end)

      it('should inject once if specified', function()
        local tag = internals.build_tag()
        enabled_metadata[internals.CONFIG_KEY].inject_times = 1
        local chunks = { ResponseBodyBuffered.new('<head></head>'), ResponseBodyBuffered.new('</head>') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)

        DatadogRUM.inject_response(response)

        assert.is.equal('<head>' .. tag .. '</head>', chunks[1]:getBytes(0, chunks[1]:length()))
        assert.is.equal('</head>', chunks[2]:getBytes(0, chunks[2]:length()))
      end)

      it('should inject twice if specified', function()
        local tag = internals.build_tag()
        enabled_metadata[internals.CONFIG_KEY].inject_times = 2
        local chunks = { ResponseBodyBuffered.new('</head></head>'), ResponseBodyBuffered.new('</head>') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)

        DatadogRUM.inject_response(response)

        assert.is.equal(string.rep(tag .. '</head>', 2), chunks[1]:getBytes(0, chunks[1]:length()))
        assert.is.equal('</head>', chunks[2]:getBytes(0, chunks[2]:length()))
      end)

      it('should inject three times if specified', function()
        local tag = internals.build_tag()
        enabled_metadata[internals.CONFIG_KEY].inject_times = 3
        local chunks = { ResponseBodyBuffered.new('</head></head>'), ResponseBodyBuffered.new('</head>') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)

        DatadogRUM.inject_response(response)

        assert.is.equal(string.rep(tag .. '</head>', 2), chunks[1]:getBytes(0, chunks[1]:length()))
        assert.is.equal(tag .. '</head>', chunks[2]:getBytes(0, chunks[2]:length()))
      end)

      it('should inject when matching across chunks', function()
        local tag = internals.build_tag()
        enabled_metadata[internals.CONFIG_KEY].inject_times = 2
        local chunks = { ResponseBodyBuffered.new('</head></hea'), ResponseBodyBuffered.new('d></head>') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)

        DatadogRUM.inject_response(response)

        assert.is.equal(tag .. '</head>', chunks[1]:getBytes(0, chunks[1]:length()))
        assert.is.equal(tag .. '</head></head>', chunks[2]:getBytes(0, chunks[2]:length()))
      end)

      it('should inject when matching below expectations across chunks', function()
        local tag = internals.build_tag()
        enabled_metadata[internals.CONFIG_KEY].inject_times = 4
        local chunks = { ResponseBodyBuffered.new('</head></hea'), ResponseBodyBuffered.new('d></head>') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)

        DatadogRUM.inject_response(response)

        assert.is.equal(tag .. '</head>', chunks[1]:getBytes(0, chunks[1]:length()))
        assert.is.equal(string.rep(tag .. '</head>', 2), chunks[2]:getBytes(0, chunks[2]:length()))
      end)

      it('should inject before the desired match', function()
        local tag = internals.build_tag()
        enabled_metadata[internals.CONFIG_KEY].inject_before = 'bar'
        local chunks = { ResponseBodyBuffered.new('foobar'), ResponseBodyBuffered.new('baz') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)

        DatadogRUM.inject_response(response)

        assert.is.equal('foo' .. tag .. 'bar', chunks[1]:getBytes(0, chunks[1]:length()))
        assert.is.equal('baz', chunks[2]:getBytes(0, chunks[2]:length()))
      end)

      it('should inject before the desired match across chunks', function()
        local tag = internals.build_tag()
        enabled_metadata[internals.CONFIG_KEY].inject_before = 'bar'
        local chunks = { ResponseBodyBuffered.new('foob'), ResponseBodyBuffered.new('arbaz') }
        local response = ResponseHandle.new(enabled_headers, enabled_metadata, chunks)

        DatadogRUM.inject_response(response)

        assert.is.equal('foo', chunks[1]:getBytes(0, chunks[1]:length()))
        assert.is.equal(tag .. 'barbaz', chunks[2]:getBytes(0, chunks[2]:length()))
      end)
    end)
  end)
end)
