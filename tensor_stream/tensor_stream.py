import torch
import TensorStream
import threading
import logging
from enum import Enum


## @defgroup pythonAPI Python API
# @brief The list of TensorStream components can be used via Python interface
# @details Here are all the classes, enums, functions described which can be used via Python to do RTMP/local stream converting to Pytorch Tensor with additional post-processing conversions
# @{

## Class with list of possible error statuses can be returned from TensorStream extension
# @warning These statuses are used only in Python wrapper that communicates with TensorStream C++ extension 
class StatusLevel(Enum):
    ## No errors
    OK = 0
    ## Need to call %TensorStream API one more time
    REPEAT = 1
    ## Some issue in %TensorStream component occured
    ERROR = 2


## Class with list of modes for logs output
# @details Used in @ref TensorStreamConverter.enable_logs() function
class LogsLevel(Enum):
    ## No logs are needed
    NONE = 0
    ## Print the indexes of processed frames
    LOW = 1
    ## Print also frame processing duration
    MEDIUM = 2
    ## Print also the detailed information about functions in callstack
    HIGH = 3


## Class with list of places the log file has to be written to
# @details Used in @ref TensorStreamConverter.enable_logs() function
class LogsType(Enum):
    ## Print all logs to file
    FILE = 1
    ## Print all logs to console
    CONSOLE = 2

## Class with supported frame output color formats
# @details Used in @ref TensorStreamConverter.read() function
class FourCC(Enum):
    ## Monochrome format, 8 bit for pixel
    Y800 = 0
    ## RGB format, 24 bit for pixel, color plane order: R, G, B
    RGB24 = 1
    ## RGB format, 24 bit for pixel, color plane order: B, G, R
    BGR24 = 2

## Algorithm used to do resize
class ResizeType(Enum):
    ## Simple algorithm without any interpolation
    NEAREST = 0
    ## Algorithm that does simple linear interpolation
    BILINEAR = 1

## Possible planes order in RGB format
class Planes(Enum):
    ## Color components R, G, B are stored in memory separately like RRRRR, GGGGG, BBBBB
    PLANAR = 0
    ## Color components R, G, B are stored in memory one by one like RGBRGBRGB
    MERGED = 1 


## Class which allow start decoding process and get Pytorch tensors with post-processed frame data
class TensorStreamConverter:
    ## Constructor of TensorStreamConverter class
    # @param[in] stream_url Path to stream should be decoded
    # @anchor repeat_number
    # @param[in] repeat_number Set how many times @ref initialize() function will try to initialize pipeline in case of any issues
    # @param[in] buffer_size Set how many processed frames can be stored in internal buffer
    # @warning Size of buffer should be less or equal to DPB
    def __init__(self, stream_url, buffer_size=10, repeat_number=1):
        self.log = logging.getLogger(__name__)
        self.log.info("Create TensorStream")
        self.tensor_stream = TensorStream.TensorStream()
        self.thread = None
        ## Amount of frames per second obtained from input bitstream, set by @ref initialize() function
        self.fps = None 
        ## Size (width and height) of frames in input bitstream, set by @ref initialize() function
        self.frame_size = None 

        self.buffer_size = buffer_size
        self.stream_url = stream_url
        self.repeat_number = repeat_number

    ## Initialization of C++ extension
    # @warning if initialization attempts exceeded @ref repeat_number, RuntimeError is being thrown
    def initialize(self):
        self.log.info("Initialize TensorStream")
        status = StatusLevel.REPEAT.value
        repeat = self.repeat_number
        while status != StatusLevel.OK.value and repeat > 0:
            status = self.tensor_stream.init(self.stream_url, self.buffer_size)
            if status != StatusLevel.OK.value:
                self.stop()
            repeat = repeat - 1

        if repeat == 0:
            raise RuntimeError("Can't initialize TensorStream")
        else:
            params = self.tensor_stream.getPars()
            self.fps = params['framerate_num'] / params['framerate_den']
            self.frame_size = (params['width'], params['height'])

    ## Enable logs from TensorStream C++ extension
    # @param[in] level Specify output level of logs, see @ref LogsLevel for supported values
    # @param[in] log_type Specify where the logs should be printed, see @ref LogsType for supported values
    def enable_logs(self, level, log_type):
        if log_type == LogsType.FILE:
            self.tensor_stream.enableLogs(level.value)
        else:
            self.tensor_stream.enableLogs(-level.value)

    ## Enable NVTX from TensorStream C++ extension
    def enable_nvtx(self):
        self.tensor_stream.enableNVTX()

    ## Read the next decoded frame, should be invoked only after @ref start() call
    # @param[in] name The unique ID of consumer. Needed mostly in case of several consumers work in different threads
    # @param[in] delay Specify which frame should be read from decoded buffer. Can take values in range [-10, 0]
    # @param[in] width Specify the width of decoded frame
    # @param[in] height Specify the height of decoded frame
    # @param[in] resize_type Algorithm used to do resize, see @ref ResizeType for supported values
    # @param[in] normalization Should final colors be normalized or not
    # @param[in] planes_pos Possible planes order in RGB format, see @ref Planes for supported values
    # @param[in] pixel_format Output FourCC of frame stored in tensor, see @ref FourCC for supported values
    # @param[in] return_index Specify whether need return index of decoded frame or not
    
    # @return Decoded frame in CUDA memory wrapped to Pytorch tensor and index of decoded frame if @ref return_index option set
    def read(self,
             name="default",
             delay=0,
             width=0,
             height=0,
             resize_type=ResizeType.NEAREST,
             normalization=False,
             planes_pos=Planes.MERGED,
             pixel_format=FourCC.RGB24,
             return_index=False,
             ):
        frame_parameters = TensorStream.FrameParameters()
        color_options = TensorStream.ColorOptions()
        color_options.normalization = normalization
        color_options.planesPos = TensorStream.Planes(planes_pos.value)
        color_options.dstFourCC = TensorStream.FourCC(pixel_format.value)
        
        resize_options = TensorStream.ResizeOptions()
        resize_options.width = width
        resize_options.height = height
        resize_options.resizeType = TensorStream.ResizeType(resize_type.value)
        
        frame_parameters.color = color_options
        frame_parameters.resize = resize_options

        #print(f"Name {name} delay {delay} width {width} height {height} resizeType {resizeType} normalization {normalization} planesPos {planesPos} pixel_format {pixel_format} return_index {return_index}")
        tensor, index = self.tensor_stream.get(name, delay, frame_parameters)
        if return_index:
            return tensor, index
        else:
            return tensor

    ## Dump the tensor to hard driver
    # @param[in] tensor Tensor which should be dumped
    # @param[in] name The name of file with dumps
    # @param[in] width Specify the width of decoded frame
    # @param[in] height Specify the height of decoded frame
    # @param[in] resize_type Algorithm used to do resize, see @ref ResizeType for supported values
    # @param[in] normalization Should final colors be normalized or not
    # @param[in] planes_pos Possible planes order in RGB format, see @ref Planes for supported values
    # @param[in] pixel_format Output FourCC of frame stored in tensor, see @ref FourCC for supported values
    def dump(self,
             tensor,
             name="default",
             width=0,
             height=0,
             resize_type=ResizeType.NEAREST,
             normalization=False,
             planes_pos=Planes.MERGED,
             pixel_format=FourCC.RGB24,):
        frame_parameters = TensorStream.FrameParameters()
        color_options = TensorStream.ColorOptions()
        color_options.normalization = normalization
        color_options.planesPos = TensorStream.Planes(planes_pos.value)
        color_options.dstFourCC = TensorStream.FourCC(pixel_format.value)
        
        resize_options = TensorStream.ResizeOptions()
        resize_options.width = width
        resize_options.height = height
        resize_options.resizeType = TensorStream.ResizeType(resize_type.value)
        
        frame_parameters.color = color_options
        frame_parameters.resize = resize_options

        self.tensor_stream.dump(tensor, name, frame_parameters)

    def _start(self):
        self.tensor_stream.start()

    ## Start processing with parameters set via @ref initialize() function
    # This functions is being executed in separate thread
    def start(self):
        self.thread = threading.Thread(target=self._start)
        self.thread.start()

    ## Close TensorStream session
    # @param[in] level Value from @ref CloseLevel
    def stop(self):
        self.log.info("Stop TensorStream")
        self.tensor_stream.close()
        if self.thread is not None:
            self.thread.join()
   
## @}