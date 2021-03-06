#include "image.h"

#include "../memorymanager.h"

#include "../debug.h"
#include "../utils.h"

#include "rectangle.h"
#include "boundingbox.h"

#include "yarng.h"

crossbowImageP crossbowImageCreate (int channels, int height, int width) {
	crossbowImageP p = NULL;
	p = (crossbowImageP) crossbowMalloc (sizeof(crossbow_image_t));
	memset(p, 0, sizeof(crossbow_image_t));

	p->channels = channels;

	/* Configure output image shape */
	p->height_ = height;
	p->width_ = width;

	/*
	 * Allocate a new decompress struct
	 * with the default error handler.
	 */
	p->info = (crossbowJpegDecompressInfoP) crossbowMalloc (sizeof(struct jpeg_decompress_struct));
	p->err = (crossbowJpegErrorP) crossbowMalloc (sizeof(struct jpeg_error_mgr));
	p->info->err = jpeg_std_error (p->err);
	jpeg_create_decompress (p->info);

	p->data = NULL;
	p->height = p->width = 0;

	p->format = HWC;

	p->isfloat = 0; /* Float representation of the image (stored in `data`) */
	p->started = 0;
	p->decoded = 0;

	p->output = NULL;
	p->offset = 0;
	return p;
}

void crossbowImageReadFromMemory (crossbowImageP p, void *data, int length) {
	nullPointerException(p);
	jpeg_mem_src (p->info, data, length);
	return;
}

void crossbowImageReadFromFile (crossbowImageP p, FILE *file) {
	nullPointerException(p);
	jpeg_stdio_src (p->info, file);
	return;
}

void crossbowImageStartDecoding (crossbowImageP p) {
	nullPointerException(p);
	/* Read file header, set default decompression parameters */
	if (jpeg_read_header (p->info, TRUE) != 1)
		err("Failed to read JPEG header\n");
	/* Try integer fast... */
	p->info->dct_method = JDCT_IFAST;
	p->info->out_color_space = JCS_RGB;
	jpeg_start_decompress(p->info);
	/* dbg("JPEG image (%d x %d x %d)\n", p->info->output_height, p->info->output_width, p->info->output_components); */
	/* JPEG images must have 3 channels */
	if (p->info->output_components != 3) {
		printf("Hm. Image has %d channels?\n", p->info->output_components);
	}
	invalidConditionException (p->info->output_components == p->channels);
	p->started = 1;
	return;
}

void crossbowImageDecode (crossbowImageP p) {
	nullPointerException(p);
	/* Is `p->info` filled? */
	invalidConditionException (p->started);
	if (p->decoded)
		return;
	p->elements = crossbowImageInputHeight (p) * crossbowImageInputWidth (p) * crossbowImageChannels (p);
	p->img = (unsigned char *) crossbowMalloc (p->elements);

	/* Decompress */

	/* Compute row stride */
	int stride = crossbowImageInputWidth (p) * crossbowImageChannels (p);
	unsigned char *buffer[1];

	/* By default, scanlines will come out in RGBRGBRGB...  order */
	while (p->info->output_scanline < (unsigned int) crossbowImageInputHeight (p)) {

		buffer[0] = p->img + p->info->output_scanline * stride;

		jpeg_read_scanlines(p->info, buffer, 1);
	}
	jpeg_finish_decompress(p->info);

	p->decoded = 1;
	return;
}

void crossbowImageCast (crossbowImageP p) {
	int i;
	nullPointerException (p);
	/* Is the image decoded? */
	invalidConditionException (p->decoded);
	if (p->isfloat)
		return;
	p->data = (float *) crossbowMalloc (p->elements * sizeof(float));
	for (i = 0; i < p->elements; i++)
		p->data[i] = (float) p->img[i];
	p->isfloat = 1;
	/* Set current height & width */
	p->height = crossbowImageInputHeight (p);
	p->width  = crossbowImageInputWidth  (p);
	return;
}

void crossbowImageTranspose (crossbowImageP p) {
    int c, h, w;
    nullPointerException (p);
	invalidConditionException (p->decoded);
	invalidConditionException (p->isfloat);
    /* Allocate new buffer */
    float *transposed = (float *) crossbowMalloc (crossbowImageCurrentElements (p) * sizeof(float));
    int src, dst;
    for (h = 0; h < p->height; ++h) {
        for (w = 0; w < p->width; ++w) {
            for (c = 0; c < p->channels; ++c) {
                src = ((h * p->width  + w) * p->channels + c);
                dst = ((c * p->height + h) * p->width    + w);
                transposed[dst] = p->data[src];
            }
        }
    }
    
    /* Free current data pointer */
    crossbowFree (p->data, crossbowImageCurrentElements (p) * sizeof(float));
    /* Assign new data pointer */
    p->data = transposed;
    return;
}

int crossbowImageInputHeight (crossbowImageP p) {
	nullPointerException(p);
	invalidConditionException (p->started);
	return p->info->output_height;
}

int crossbowImageInputWidth (crossbowImageP p) {
	nullPointerException(p);
	invalidConditionException (p->started);
	return p->info->output_width;
}

int crossbowImageCurrentHeight (crossbowImageP p) {
	nullPointerException(p);
	invalidConditionException (p->isfloat);
	return p->height;
}

int crossbowImageCurrentWidth (crossbowImageP p) {
	nullPointerException(p);
	invalidConditionException (p->isfloat);
	return p->width;
}

int crossbowImageCurrentElements (crossbowImageP p) {
	return (crossbowImageCurrentHeight (p) *
			crossbowImageCurrentWidth  (p) * crossbowImageChannels (p));
}

int crossbowImageOutputHeight (crossbowImageP p) {
	nullPointerException(p);
	return p->height_;
}

int crossbowImageOutputWidth (crossbowImageP p) {
	nullPointerException(p);
	return p->width_;
}

int crossbowImageChannels (crossbowImageP p) {
	nullPointerException(p);
	return p->channels;
}

void crossbowImageCrop (crossbowImageP p, int height, int width, int top, int left) {
	int x, y;
	int start, offset = 0;
	float  *input;
	float *output;

	nullPointerException(p);
	invalidConditionException (p->isfloat);

	/*
	dbg("Crop image from (%d x %d) to (%d x %d) starting at (%d, %d)\n",
			crossbowImageCurrentHeight (p),
			crossbowImageCurrentWidth  (p),
			height,
			width,
			top, left);
	*/

	/* Allocate new buffer */
	float *cropped = (float *) crossbowMalloc (crossbowImageChannels (p) * height * width * sizeof(float));

	/* Compute new row size */
	int rowsize = width * crossbowImageChannels (p);

	for (y = 0; y < height; ++y) {
		/* Move input data pointer to top-left position */
		start = ((top + y) * crossbowImageChannels (p) * crossbowImageCurrentWidth (p)) + (left * crossbowImageChannels (p));
		input = p->data + start;
		/* Copy `width` pixels to output (a complete row) */
		output = cropped + offset;
		for (x = 0; x < rowsize; ++x)
			output[x] = input[x];
		/* Move output data pointer */
		offset += rowsize;
	}
	/* Free current data pointer */
	crossbowFree (p->data, crossbowImageCurrentElements (p) * sizeof(float));
	/* Assign new data pointer */
	p->data = cropped;
	/* Set current data, height & width */
	p->height = height;
	p->width  =  width;
	return;
}

void crossbowImageRandomFlipLeftRight (crossbowImageP p) {
	nullPointerException(p);
	return;
}

void crossbowImageDistortColor (crossbowImageP p) {
	nullPointerException(p);
	return;
}

void crossbowImageRandomBrightness (crossbowImageP p, float delta) {
	nullPointerException(p);
	invalidArgumentException(delta > 0);

	float brightness = crossbowYarngNext (-delta, delta);
	crossbowImageAdjustBrightness (p, brightness);

	return;
}

/**
 * Requires float representation.
 *
 * The value `delta` is added to all components of the image. 
 * `delta` should be in the range `[0,1)`. Pixels should also 
 * be in that range.
 */
void crossbowImageAdjustBrightness (crossbowImageP p, float delta) {
	nullPointerException(p);
	invalidConditionException(p->isfloat);
	invalidArgumentException(((delta >= 0) && (delta < 1)));
	int i;
	int elements = crossbowImageCurrentElements (p);
	for (i = 0; i < elements; ++i)
		p->data[i] += delta;
	return;
}

void crossbowImageRandomContrast (crossbowImageP p, float lower, float upper) {
	nullPointerException(p);
	invalidArgumentException(lower >= 0);
	invalidArgumentException(lower <= upper);

	float contrast = crossbowYarngNext (lower, upper);
	crossbowImageAdjustContrast (p, contrast);
	return;
}

/**
 * Requires float representation.
 * 
 * Contrast is adjusted independently for 
 * each channel of each image.
 * 
 * For each channel, the function computes 
 * the mean of the pixels in the channel.
 * 
 * It then adjusts each component `x` of a
 * pixel:
 *    x = (x - mean) * factor + mean
 */
void crossbowImageAdjustContrast (crossbowImageP p, float factor) {
	nullPointerException(p);
	invalidConditionException(p->isfloat);
	/* Compute mean per channel */
	int i;
	float R, G, B;
	float N = (float) (crossbowImageCurrentHeight (p) * crossbowImageCurrentWidth (p));
	int elements = crossbowImageCurrentElements (p);
	R = G = B = 0;
	for (i = 0; i < elements; i += p->channels) {
		R += p->data[i + 0];
		G += p->data[i + 1];
		B += p->data[i + 2];
	}
	R /= N;
	G /= N;
	B /= N;
	/* Adjust contrast by factor */
	for (i = 0; i < elements; i += p->channels) {
		p->data[i + 0] = (p->data[i + 0] - R) * factor + R;
		p->data[i + 1] = (p->data[i + 1] - G) * factor + G;
		p->data[i + 2] = (p->data[i + 2] - B) * factor + B;
	}
	return;
}

void crossbowImageRandomSaturation (crossbowImageP p) {
	crossbowImageAdjustSaturation (p, 0);
	return;
}

/**
 * Requires float representation
 *
 * The image saturation is adjusted by  converting 
 * the image to HSV and multiplying the saturation 
 * channel (S) by `factor` and clipping. 
 * 
 * The image is then converted back to RGB.
 */
void crossbowImageAdjustSaturation (crossbowImageP p, float factor) {
	nullPointerException(p);
	(void) factor;
	unsupportedOperationException ();
	return;
}

void crossbowImageRandomHue (crossbowImageP p) {
	crossbowImageAdjustHue (p, 0);
	return;
}

/**
 * Requires float representation.
 * 
 * image` is an RGB image.  
 * The image hue is adjusted by converting the image 
 * to HSV and rotating the hue channel (H) by `delta`.  
 * 
 * The image is then converted back to RGB.
 */
void crossbowImageAdjustHue (crossbowImageP p, float delta) {
	nullPointerException(p);
	(void) delta;
	unsupportedOperationException ();
	return;
}

/**
 * Requires float representation.
 *
 * Any value less that `lower` is set to `lower`; and
 * any value greater than `upper` is set to `upper`.
 */
void crossbowImageClipByValue (crossbowImageP p, float lower, float upper) {
	int i;
	int elements;
	nullPointerException(p);
	invalidConditionException(p->isfloat);
	elements = crossbowImageCurrentElements (p);
	for (i = 0; i < elements; ++i) {
		if (p->data [i] < lower)
			p->data [i] = lower;
		if (p->data [i] > upper)
			p->data [i] = upper;
	}
	return;
}

/**
 * Notes:
 *
 * lower: Lower saturation
 * upper: Upper saturation
 * delta: Delta HUE
 */
void crossbowImageRandomHSVInYIQ (crossbowImageP p, float lower, float upper, float delta) {
	nullPointerException(p);
	(void) lower;
	(void) upper;
	(void) delta;
	return;
}

void crossbowImageAdjustHSVInYIQ (crossbowImageP p) {
	nullPointerException(p);
	return;
}

void crossbowImageMultiply (crossbowImageP p, float value) {
	int i;
	int elements;
	nullPointerException(p);
	invalidConditionException(p->isfloat);
	elements = crossbowImageCurrentElements (p);
	for (i = 0; i < elements; ++i)
		p->data [i] = p->data [i] * value;
	return;
}

void crossbowImageSubtract (crossbowImageP p, float value) {
	int i;
	int elements;
	nullPointerException(p);
	invalidConditionException(p->isfloat);
	elements = crossbowImageCurrentElements (p);
	for (i = 0; i < elements; ++i)
		p->data [i] -= value;
	return;
}

char *crossbowImageString (crossbowImageP p) {
	nullPointerException(p);
	if (! p->decoded)
		return NULL;
	char s [1024];
	int offset;
	size_t remaining;
	memset (s, 0, sizeof(s));
	remaining = sizeof(s) - 1;
	offset = 0;
	crossbowStringAppend (s, &offset, &remaining, "input (%4d x %4d x %d) output (%d x %4d x %4d)",
			/* Input format */
			crossbowImageInputHeight (p), crossbowImageInputWidth (p), crossbowImageChannels (p),
			/* Output format */
			crossbowImageChannels (p), p->height_, p->width_);

	return crossbowStringCopy (s);
}

char *crossbowImageInfo (crossbowImageP p) {
	nullPointerException(p);
	invalidConditionException (p->decoded);
	invalidConditionException (p->isfloat);
	char s [256];
	int offset;
	size_t remaining;
	memset (s, 0, sizeof(s));
	remaining = sizeof(s) - 1;
	offset = 0;
	crossbowStringAppend (s, &offset, &remaining, "%d x %d x %d", p->height, p->width, p->channels);
	return crossbowStringCopy (s);
}

double crossbowImageChecksum (crossbowImageP p) {
	int i;
	double checksum = 0.0D;

	nullPointerException(p);
	invalidConditionException (p->decoded);
	invalidConditionException (p->isfloat);

	int elements = crossbowImageCurrentElements (p);
	for (i = 0; i < elements; ++i)
		checksum += (double) p->data[i];
	return checksum;
}

void crossbowImageDump (crossbowImageP p, int pixels) {
	int i, j;
	nullPointerException(p);
	invalidConditionException (p->decoded);

	char *info = crossbowImageInfo(p);
	fprintf(stdout, "=== [image: %s, checksum %.5f] ===\n", info, crossbowImageChecksum(p));
	crossbowStringFree(info);

	/* Print the first few `pixels` (x 3) */
	for (i = 0; i < pixels; ++i) {
		for (j = 0; j < 3; ++j)
			fprintf (stdout, "%3u ", p->img[i * 3 + j]);
		fprintf(stdout, "\n");
	}
	fprintf(stdout, "...\n");

	fprintf(stdout, "=== [End of image dump] ===\n");
	fflush(stdout);
}

void crossbowImageDumpAsFloat (crossbowImageP p, int pixels) {
	int i, j;
	nullPointerException(p);
	invalidConditionException (p->decoded);

	char *info = crossbowImageInfo(p);
	fprintf(stdout, "=== [image: %s, checksum %.5f] ===\n", info, crossbowImageChecksum(p));
	crossbowStringFree(info);

	/* Print the first few `pixels` (x 3) */
	for (i = 0; i < pixels; ++i) {
		for (j = 0; j < 3; ++j)
			fprintf (stdout, "%13.8f ", p->data[i * 3 + j]);
		fprintf(stdout, "\n");
	}
	fprintf(stdout, "...\n");

	fprintf(stdout, "=== [End of image dump] ===\n");
	fflush(stdout);
}


int crossbowImageCopy (crossbowImageP p, void * buffer, int offset, int limit) {
    
    nullPointerException (p);
    invalidConditionException (p->decoded);
    invalidConditionException (p->isfloat);
    
    int length = crossbowImageCurrentElements (p) * sizeof(float);
    
    if (limit > 0)
        invalidConditionException ((offset + length) < (limit + 1));
    
    /* Copy p->data to buffer (starting at offset) */
    memcpy ((void *)(buffer + offset), (void *)(p->data), length);
    return length;
}

void crossbowImageFree (crossbowImageP p) {
	if (! p)
		return;
	if (p->img)
		crossbowFree (p->img, p->elements);
	if (p->data)
		crossbowFree (p->data, crossbowImageCurrentElements (p) * sizeof(float));
	/* Release JPEG library state */
	jpeg_destroy_decompress(p->info);
	crossbowFree (p->info, sizeof(struct jpeg_decompress_struct));
	crossbowFree (p->err,  sizeof(struct jpeg_error_mgr));
	crossbowFree (p, sizeof(crossbow_image_t));
	return;
}

/* Assuming M inputs & N outputs */
static inline void crossbowComputeInterpolationWeights
	(crossbowInterpolationWeightP *weights, long N, long M, float scale) {

	float in;
	long idx;

	weights [N]->lower = 0;
	weights [N]->upper = 0;

	for (idx = N - 1; idx >= 0; --idx) {

		in = idx * scale;

		weights [idx]->lower = (long) in;
		weights [idx]->upper = min(weights[idx]->lower + 1, M - 1);
		weights [idx]->lerp = in - weights[idx]->lower;
	}
}

static inline float crossbowComputeLerp (float topleft, float topright, float bottomleft, float bottomright, float xlerp, float ylerp) {

	float top = topleft + (topright - topleft) * xlerp;
	float bottom = bottomleft + (bottomright - bottomleft) * xlerp;

	return (top + (bottom - top) * ylerp);
}

static void crossbowResizeImage (
	float *input,
	long C,
	long H, /* Input dimensions */
	long W,
	long H_, /* Output dimensions */
	long W_,
	crossbowInterpolationWeightP *X,
	crossbowInterpolationWeightP *Y,
	float *output) {

	long x, y;

	float topleft [3], topright [3], bottomleft [3], bottomright [3];

	/* Resize image from (C, H, W) to (C, H_, W_)... */

	long row  = W  * C;
	long row_ = W_ * C;

	(void) H;

	float *outputP = output;

	for (y = 0; y < H_; ++y) {

		float *_y_lower = input + Y [y]->lower * row;
		float *_y_upper = input + Y [y]->upper * row;

		float  _y_lerp  = Y[y]->lerp;

		for (x = 0; x < W_; ++x) {

			long  _x_lower = X [x]->lower;
			long  _x_upper = X [x]->upper;
			float _x_lerp  = X [x]->lerp;

			/* Read channel #0 (R) */
			topleft     [0] = _y_lower [_x_lower + 0];
			topright    [0] = _y_lower [_x_upper + 0];
			bottomleft  [0] = _y_upper [_x_lower + 0];
			bottomright [0] = _y_upper [_x_upper + 0];

			/* Read channel #1 (G) */
			topleft     [1] = _y_lower [_x_lower + 1];
			topright    [1] = _y_lower [_x_upper + 1];
			bottomleft  [1] = _y_upper [_x_lower + 1];
			bottomright [1] = _y_upper [_x_upper + 1];

			/* Read channel #2 (B) */
			topleft     [2] = _y_lower [_x_lower + 2];
			topright    [2] = _y_lower [_x_upper + 2];
			bottomleft  [2] = _y_upper [_x_lower + 2];
			bottomright [2] = _y_upper [_x_upper + 2];

			/* Compute output */
			outputP [x * C + 0] = crossbowComputeLerp (topleft [0], topright [0], bottomleft [0], bottomright [0], _x_lerp, _y_lerp);
			outputP [x * C + 1] = crossbowComputeLerp (topleft [1], topright [1], bottomleft [1], bottomright [1], _x_lerp, _y_lerp);
			outputP [x * C + 2] = crossbowComputeLerp (topleft [2], topright [2], bottomleft [2], bottomright [2], _x_lerp, _y_lerp);
		}

		/* Move output pointer to next row */
		outputP = outputP + row_;
	}
	return;
}

static inline float crossbowCalculateResizeScale (int inputSize, int outputSize) {
	return (float) inputSize / (float) outputSize;
}

void crossbowImageResize (crossbowImageP p, int height, int width) {
	int i;
	nullPointerException(p);

	/* Allocate interpolation weights on the X and Y dimensions */

	crossbowInterpolationWeightP *X =
			(crossbowInterpolationWeightP *) crossbowMalloc ((width  + 1) * sizeof(crossbowInterpolationWeightP));

	crossbowInterpolationWeightP *Y =
			(crossbowInterpolationWeightP *) crossbowMalloc ((height + 1) * sizeof(crossbowInterpolationWeightP));

	for (i = 0; i < width + 1; ++i)
		X[i] = (crossbowInterpolationWeightP) crossbowMalloc (sizeof(crossbow_interpolation_weight_t));

	for (i = 0; i < height + 1; ++i)
		Y[i] = (crossbowInterpolationWeightP) crossbowMalloc (sizeof(crossbow_interpolation_weight_t));

	crossbowComputeInterpolationWeights (X,  width, crossbowImageCurrentWidth  (p), crossbowCalculateResizeScale (crossbowImageCurrentWidth  (p),  width));
	crossbowComputeInterpolationWeights (Y, height, crossbowImageCurrentHeight (p), crossbowCalculateResizeScale (crossbowImageCurrentHeight (p), height));

	for (i = 0; i < width + 1; ++i) {
		X[i]->lower *= p->channels;
		X[i]->upper *= p->channels;
	}

	/* Allocate new buffer */
	float *resized = (float *) crossbowMalloc (crossbowImageChannels (p) * height * width * sizeof(float));

	crossbowResizeImage (
		p->data,
		p->channels,
		crossbowImageCurrentHeight (p),
		crossbowImageCurrentWidth  (p),
		height,
		width,
		X,
		Y,
		resized
	);

	/* Free interpolation weights */

	for (i = 0; i < width + 1; ++i)
		crossbowFree (X[i], sizeof(crossbow_interpolation_weight_t));

	for (i = 0; i < height + 1; ++i)
		crossbowFree (Y[i], sizeof(crossbow_interpolation_weight_t));

	crossbowFree (X, ((width  + 1) * sizeof(crossbowInterpolationWeightP)));
	crossbowFree (Y, ((height + 1) * sizeof(crossbowInterpolationWeightP)));

	/* Free current data pointer */
	crossbowFree (p->data, crossbowImageCurrentElements (p) * sizeof(float));
	/* Assign new data pointer */
	p->data = resized;
	/* Set current data, height & width */
	p->height = height;
	p->width  =  width;

	return;
}

static unsigned generateRandomCrop (crossbowRectangleP crop, int originalHeight, int originalWidth, float *area, float aspectRatio) {

	/* If any of height, width, area, aspect ratio is less that 0, return 0; */

	float minArea = area[0] * originalWidth * originalHeight;
	float maxArea = area[1] * originalWidth * originalHeight;

	int minHeight = (int) lrintf (sqrt (minArea / aspectRatio));
	int maxHeight = (int) lrintf (sqrt (maxArea / aspectRatio));

	/* Find smaller max height s.t. round (maxHeight x acpectRatio) <= originalWidth */
	if (lrintf (maxHeight * aspectRatio) > originalWidth) {

		float epsilon = 0.0000001;
		maxHeight = (int) ((originalWidth + 0.5 - epsilon) / aspectRatio);
	}

	if (maxHeight > originalHeight)
		maxHeight = originalHeight;

	if (minHeight > maxHeight)
		minHeight = maxHeight;

	if (minHeight < maxHeight)
		/* Generate a random number of the closed range [0, (maxHeight - minHeight)]*/
		minHeight += crossbowYarngNext (0, maxHeight - minHeight + 1);

	int minWidth = (int) lrintf (minHeight * aspectRatio);

	float newArea = (float) (minHeight * minWidth);

	/* Deal with rounding errors */

	if (newArea < minArea) {
		minHeight += 1;
		minWidth = (int) lrintf (minHeight * aspectRatio);
		newArea = (float) (minHeight * minWidth);
	}

	if (newArea > maxArea) {
		minHeight -= 1;
		minWidth = (int) lrintf (minHeight * aspectRatio);
		newArea = (float) (minHeight * minWidth);
	}

	if (newArea < minArea || newArea > maxArea || minWidth > originalWidth || minHeight > originalHeight || minWidth <= 0 || minHeight <= 0)
		return 0;

	int x = 0;
	if (minWidth < originalWidth) {
		x = crossbowYarngNext (0, originalWidth - minWidth);
	}

	int y = 0;
	if (minHeight < originalHeight) {
		y = crossbowYarngNext (0, originalHeight - minHeight);
	}

	crop->xmin = x;
	crop->ymin = y;
	crop->xmax = x + minWidth;
	crop->ymax = y + minHeight;

	return 1;
}

void crossbowImageSampleDistortedBoundingBox (crossbowImageP p, crossbowArrayListP boxes, float coverage, float *ratio, float *area, int attempts, int *height, int *width, int *top, int *left) {
	int idx;
	nullPointerException(p);

	int currentHeight = (int) crossbowImageCurrentHeight (p);
	int currentWidth  = (int) crossbowImageCurrentWidth  (p);

	/* Convert bounding boxes to rectangles. If there are none, use entire image by default */
	crossbowArrayListP rectangles = NULL;
	if (boxes) {
		rectangles = crossbowArrayListCreate (crossbowArrayListSize (boxes));
		for (idx = 0; idx < crossbowArrayListSize (boxes); ++idx) {

			crossbowBoundingBoxP box = (crossbowBoundingBoxP) crossbowArrayListGet (boxes, idx);
			int xmin = box->xmin * currentWidth;
			int ymin = box->ymin * currentHeight;
			int xmax = box->xmax * currentWidth;
			int ymax = box->ymax * currentHeight;

			crossbowRectangleP rectangle = crossbowRectangleCreate (xmin, ymin, xmax, ymax);
			crossbowArrayListSet (rectangles, idx, rectangle);
		}
	} else {
		rectangles = crossbowArrayListCreate (1);
		crossbowArrayListSet (rectangles, 0, crossbowRectangleCreate (0, 0, currentWidth, currentHeight));
	}

	crossbowRectangleP crop = crossbowRectangleCreate (0, 0, 0, 0);
	unsigned generated = 0;
	int i;
	for (i = 0; i < attempts; ++i) {
		float random = 0.1;
		/* Sample aspect ratio (within bounds) */
		float sample = random * (ratio[1] - ratio[0]) + ratio[0];
		if (generateRandomCrop (crop, currentHeight, currentWidth, area, sample)) {

			if (crossbowRectangleCovers(crop, coverage, rectangles)) {
				generated = 1;
				break;
			}
		}
	}
	if (! generated)
		crossbowRectangleSet (crop, 0, 0, currentWidth, currentHeight);

	/* Determine cropping parameters for the bounding box */
	*width  = crop->xmax - crop->xmin;
	*height = crop->ymax - crop->ymin;

	*top  = crop->xmin;
	*left = crop->ymin;

	/* Ensure sampled bounding box fits current image dimensions */
	invalidConditionException (currentWidth  >= (*left + *width ));
	invalidConditionException (currentHeight >= (*top  + *height));

	/* Free local state */

	crossbowRectangleFree (crop);
	for (i = 0; i < crossbowArrayListSize (rectangles); ++i) {
		crossbowRectangleP rect = (crossbowRectangleP) crossbowArrayListGet (rectangles, i);
		crossbowRectangleFree (rect);
	}
	crossbowArrayListFree (rectangles);

	return;
}

