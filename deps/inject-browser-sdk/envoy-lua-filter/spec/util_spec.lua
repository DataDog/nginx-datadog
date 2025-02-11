require 'busted.runner' ()

_G._TEST = true
local DatadogRUM = require('src.datadog_rum')
local internals = DatadogRUM.__internals

describe('DatadogRUM', function()
  teardown(function()
    _G._TEST = nil
  end)

  describe('util', function()
    describe('value_type_or_default', function()
      it('should return the value on type match', function()
        local default = 'foo'
        assert.is_not.equal(default, internals.value_type_or_default(42, 'number', default))
        assert.is_not.equal(default, internals.value_type_or_default({ ['foo'] = 'bar' }, 'table', default))
        assert.is_not.equal(default, internals.value_type_or_default(function() return 42 end, 'function', default))
      end)
      it('should return the default on type mismatch', function()
        local default = 'foo'
        assert.is.equal(default, internals.value_type_or_default(nil, 'string', default))
        assert.is.equal(default, internals.value_type_or_default(42, 'string', default))
        assert.is.equal(default, internals.value_type_or_default({ ['foo'] = 'bar' }, 'string', default))
        assert.is.equal(default, internals.value_type_or_default(function() return 42 end, 'string', default))
      end)
      it('should return the default on nil, even if requested', function()
        local default = 'foo'
        assert.is.equal(default, internals.value_type_or_default(nil, 'nil', default))
      end)
    end)

    describe('serialize_obj', function()
      it('should serialize an array', function()
        local obj = { 'foo', 42 }
        local serialized = internals.serialize_obj(obj)
        assert.is.equal('{"1":"foo","2":42,}', serialized)
      end)
      it('should serialize a string', function()
        local obj = 'foo'
        local serialized = internals.serialize_obj(obj)
        assert.is.equal('"foo"', serialized)
      end)
      it('should serialize a number', function()
        local obj = 42
        local serialized = internals.serialize_obj(obj)
        assert.is.equal('42', serialized)
      end)
      it('should serialize a boolean', function()
        local obj = true
        local serialized = internals.serialize_obj(obj)
        assert.is.equal('true', serialized)
      end)
      it('should not serialize a function', function()
        local obj = function() return 42 end
        local serialized = internals.serialize_obj(obj)
        assert.is.equal('', serialized)
      end)
      it('should not serialize nil', function()
        local obj = nil
        local serialized = internals.serialize_obj(obj)
        assert.is.equal('', serialized)
      end)
    end)

    describe('parse_config', function()
      it('should return the defaults on nil', function()
        ---@diagnostic disable-next-line: param-type-mismatch
        local sdk, cdn, init = internals.parse_config(nil)
        assert.is.equal(internals.DEFAULT_SDK_VERSION, sdk)
        assert.is.equal(internals.DEFAULT_CDN_REGION, cdn)
        assert.is.equal(internals.DEFAULT_INIT_CONFIG, init)
      end)
      it('should return the defaults on empty', function()
        local sdk, cdn, init = internals.parse_config({})
        assert.is.equal(internals.DEFAULT_SDK_VERSION, sdk)
        assert.is.equal(internals.DEFAULT_CDN_REGION, cdn)
        assert.is.equal(internals.DEFAULT_INIT_CONFIG, init)
      end)
      it('should return the defaults on partial', function()
        local sdk, cdn, init = internals.parse_config({ ['cdn_region'] = 'eu1', ['rum'] = { ['foo'] = 'bar' } })
        assert.is.equal(internals.DEFAULT_SDK_VERSION, sdk)
        assert.is.equal('eu1', cdn)
        assert.is.equal('{"foo":"bar",}', init)
      end)
      it('should return the defaults on type mismatch', function()
        local sdk, cdn, init = internals.parse_config({ ['major_version'] = '4', ['rum'] = 'bar' })
        assert.is.equal(internals.DEFAULT_SDK_VERSION, sdk)
        assert.is.equal(internals.DEFAULT_CDN_REGION, cdn)
        assert.is.equal(internals.DEFAULT_INIT_CONFIG, init)
      end)
    end)
  end)
end)
