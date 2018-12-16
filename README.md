# libjpeg-ruby
libjpeg interface for ruby.

## Installation

```ruby
gem 'libjpeg-ruby'
```

And then execute:

    $ bundle

Or install it yourself as:

    $ gem install libjpeg-ruby

## Usage

### decode sample

```ruby
require 'jpeg'

dec = JPEG::Decoder.new(:pixel_format => :BGR)
raw = dec << IO.binread("test.jpg")

p raw.meta

IO.binwrite("test.bgr", raw)
```

#### decode options
| option | value type | description |
|---|---|---|
| :pixel_format | String or Symbol | output format |
| :output_gamma | Float | gamma value |
| :fancy_upsampling | Boolean | T.B.D |
| :do_smoothing | Boolean | T.B.D |
| :opt_dither | [{str}MODE, {bool}2PASS, {int}NUM_COLORS] | T.B.D |
| :use_1ass_quantizer | Boolean | T.B.D |
| :use_external_colormap | Boolean | T.B.D |
| :use_2pass_quantizer | Boolean | T.B.D |
| :without_meta | Boolean | T.B.D |
| :expand_colormap | Booblean | T.B.D |
| :scale | Rational or Float | T.B.D |

#### supported output format
RGB RGB24 YUV422 YUYV RGB565 YUV444 YCbCr BGR BGR24 RGBX RGB32 BGRX BGR32 

### decode sample

```ruby
require 'jpeg'

dec = JPEG::Encoder.new(640, 480, :pixel_format => :YCbCr)

IO.binwrite("test.jpg", enc << IO.binread("test.raw"))
```
#### encode option
#### decode options
| option | value type | description |
|---|---|---|
| :pixel_fromat | String or Symbol | input format |
| :quality | Integer | encode quality (0-100) |
| :scale | Rational or Float | |

