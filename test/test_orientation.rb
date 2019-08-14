require 'test/unit'
require 'pathname'
require 'jpeg'

class TestOrientation < Test::Unit::TestCase
  DATA_DIR  = Pathname($0).expand_path.dirname + "data"

  TL        = (20 * 160) + 20
  TR        = (20 * 160) + 140
  BL        = (140 * 160) + 20
  BR        = (140 * 160) + 140


  def unpack(raw)
    ret = []
    dat = raw.unpack("C*")

    ret << dat.shift(3) until dat.empty?

    return ret
  end

  test "decode orientation 1" do
    dec = JPEG::Decoder.new(:pixel_format => :RGB,
                            :orientation => true)

    dat = (DATA_DIR + "orientation-1.jpg").binread
    raw = assert_nothing_raised {dec << dat}
    met = raw.meta
    pix = unpack(raw)

    assert_equal(160, met.width)
    assert_equal(160 * met.num_components, met.stride)
    assert_equal(160, met.height)

    assert_equal([0xfe, 0x00, 0x00], pix[TL])
    assert_equal([0x00, 0xff, 0x01], pix[TR])
    assert_equal([0xff, 0xff, 0x00], pix[BL])
    assert_equal([0x00, 0x00, 0xfe], pix[BR])
  end

  test "decode orientation 2" do
    dec = JPEG::Decoder.new(:pixel_format => :RGB,
                            :orientation => true)

    dat = (DATA_DIR + "orientation-2.jpg").binread
    raw = assert_nothing_raised {dec << dat}
    met = raw.meta
    pix = unpack(raw)

    assert_equal(160, met.width)
    assert_equal(160 * met.num_components, met.stride)
    assert_equal(160, met.height)

    assert_equal([0xfe, 0x00, 0x00], pix[TR])
    assert_equal([0x00, 0xff, 0x01], pix[TL])
    assert_equal([0xff, 0xff, 0x00], pix[BR])
    assert_equal([0x00, 0x00, 0xfe], pix[BL])
  end

  test "decode orientation 3" do
    dec = JPEG::Decoder.new(:pixel_format => :RGB,
                            :orientation => true)

    dat = (DATA_DIR + "orientation-3.jpg").binread
    raw = assert_nothing_raised {dec << dat}
    met = raw.meta
    pix = unpack(raw)

    assert_equal(160, met.width)
    assert_equal(160 * met.num_components, met.stride)
    assert_equal(160, met.height)

    assert_equal([0xfe, 0x00, 0x00], pix[BR])
    assert_equal([0x00, 0xff, 0x01], pix[BL])
    assert_equal([0xff, 0xff, 0x00], pix[TR])
    assert_equal([0x00, 0x00, 0xfe], pix[TL])
  end

  test "decode orientation 4" do
    dec = JPEG::Decoder.new(:pixel_format => :RGB,
                            :orientation => true)

    dat = (DATA_DIR + "orientation-4.jpg").binread
    raw = assert_nothing_raised {dec << dat}
    met = raw.meta
    pix = unpack(raw)

    assert_equal(160, met.width)
    assert_equal(160 * met.num_components, met.stride)
    assert_equal(160, met.height)

    assert_equal([0xfe, 0x00, 0x00], pix[BL])
    assert_equal([0x00, 0xff, 0x01], pix[BR])
    assert_equal([0xff, 0xff, 0x00], pix[TL])
    assert_equal([0x00, 0x00, 0xfe], pix[TR])
  end

  test "decode orientation 5" do
    dec = JPEG::Decoder.new(:pixel_format => :RGB,
                            :orientation => true)

    dat = (DATA_DIR + "orientation-5.jpg").binread
    raw = assert_nothing_raised {dec << dat}
    met = raw.meta
    pix = unpack(raw)

    assert_equal(160, met.width)
    assert_equal(160 * met.num_components, met.stride)
    assert_equal(160, met.height)

    assert_equal([0xfe, 0x00, 0x00], pix[TL])
    assert_equal([0x00, 0xff, 0x01], pix[BL])
    assert_equal([0xff, 0xff, 0x00], pix[TR])
    assert_equal([0x00, 0x00, 0xfe], pix[BR])
  end

  test "decode orientation 6" do
    dec = JPEG::Decoder.new(:pixel_format => :RGB,
                            :orientation => true)

    dat = (DATA_DIR + "orientation-6.jpg").binread
    raw = assert_nothing_raised {dec << dat}
    met = raw.meta
    pix = unpack(raw)

    assert_equal(160, met.width)
    assert_equal(160 * met.num_components, met.stride)
    assert_equal(160, met.height)

    assert_equal([0xfe, 0x00, 0x00], pix[TR])
    assert_equal([0x00, 0xff, 0x01], pix[BR])
    assert_equal([0xff, 0xff, 0x00], pix[TL])
    assert_equal([0x00, 0x00, 0xfe], pix[BL])
  end

  test "decode orientation 7" do
    dec = JPEG::Decoder.new(:pixel_format => :RGB,
                            :orientation => true)

    dat = (DATA_DIR + "orientation-7.jpg").binread
    raw = assert_nothing_raised {dec << dat}
    met = raw.meta
    pix = unpack(raw)

    assert_equal(160, met.width)
    assert_equal(160 * met.num_components, met.stride)
    assert_equal(160, met.height)

    assert_equal([0xfe, 0x00, 0x00], pix[BR])
    assert_equal([0x00, 0xff, 0x01], pix[TR])
    assert_equal([0xff, 0xff, 0x00], pix[BL])
    assert_equal([0x00, 0x00, 0xfe], pix[TL])
  end

  test "decode orientation 8" do
    dec = JPEG::Decoder.new(:pixel_format => :RGB,
                            :orientation => true)

    dat = (DATA_DIR + "orientation-8.jpg").binread
    raw = assert_nothing_raised {dec << dat}
    met = raw.meta
    pix = unpack(raw)

    assert_equal(160, met.width)
    assert_equal(160 * met.num_components, met.stride)
    assert_equal(160, met.height)

    assert_equal([0xfe, 0x00, 0x00], pix[BL])
    assert_equal([0x00, 0xff, 0x01], pix[TL])
    assert_equal([0xff, 0xff, 0x00], pix[BR])
    assert_equal([0x00, 0x00, 0xfe], pix[TR])
  end
end
