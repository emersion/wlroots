#include <pixman.h>
#include <stdlib.h>
#include <stdio.h>
#include <wlr/types/wlr_matrix.h>

static void print_pixman_f_transform(const struct pixman_f_transform *tr) {
	printf("%f %f %f\n", tr->m[0][0], tr->m[0][1], tr->m[0][2]);
	printf("%f %f %f\n", tr->m[1][0], tr->m[1][1], tr->m[1][2]);
	printf("%f %f %f\n", tr->m[2][0], tr->m[2][1], tr->m[2][2]);
}

static void print_wlr_matrix(float m[static 9]) {
	printf("%f %f %f\n", m[0], m[1], m[2]);
	printf("%f %f %f\n", m[3], m[4], m[5]);
	printf("%f %f %f\n", m[6], m[7], m[8]);
}

int main(void) {
	/*struct pixman_f_transform ftr;
	pixman_f_transform_init_identity(&ftr);
	pixman_f_transform_translate(&ftr, NULL, 10, 10);*/

	float mat[9];
	wlr_matrix_identity(mat);
	wlr_matrix_translate(mat, 10, 10);
	printf("wlroots:\n");
	print_wlr_matrix(mat);

	struct pixman_f_transform ftr;
	ftr.m[0][0] = mat[0];
	ftr.m[0][1] = mat[1];
	ftr.m[0][2] = mat[2];
	ftr.m[1][0] = mat[3];
	ftr.m[1][1] = mat[4];
	ftr.m[1][2] = mat[5];
	ftr.m[2][0] = mat[6];
	ftr.m[2][1] = mat[7];
	ftr.m[2][2] = mat[8];

	printf("pixman:\n");
	print_pixman_f_transform(&ftr);

	pixman_color_t white = { 0xFFFF, 0xFFFF, 0xFFFF, 0xFFFF };
	pixman_image_t *white_img = pixman_image_create_solid_fill(&white);

	pixman_color_t red = { 0xFFFF, 0x0000, 0x0000, 0xFFFF };
	pixman_image_t *red_img = pixman_image_create_solid_fill(&red);

	struct pixman_transform tr;
	pixman_transform_from_pixman_f_transform(&tr, &ftr);
	pixman_transform_invert(&tr, &tr); // pixman image transforms are destination-to-source

	// Create a 100x100 source image filled with red
	void *src_data = malloc(100 * 100 * 4);
	pixman_image_t *src_img = pixman_image_create_bits(PIXMAN_r8g8b8a8, 100, 100, src_data, 100 * 4);
	pixman_image_composite32(PIXMAN_OP_SRC, red_img, NULL, src_img, 0, 0, 0, 0, 0, 0, 100, 100);
	pixman_image_set_transform(src_img, &tr);

	// Create a 256x256 final image filled with white
	void *data = malloc(256 * 256 * 4);
	pixman_image_t *img = pixman_image_create_bits(PIXMAN_r8g8b8a8, 256, 256, data, 256 * 4);
	pixman_image_composite32(PIXMAN_OP_SRC, white_img, NULL, img, 0, 0, 0, 0, 0, 0, 256, 256);

	// Blend the source image onto the final image, given the matrix
	pixman_image_composite32(PIXMAN_OP_OVER, src_img, NULL, img, 0, 0, 0, 0, 0, 0, 256, 256);

	FILE *f = fopen("/tmp/out.raw", "w+");
	fwrite(pixman_image_get_data(img), 1, pixman_image_get_stride(img) * pixman_image_get_height(img), f);
	fclose(f);

	// Convert to PNG using:
	// convert -depth 8 -size 256x256 RGBA:/tmp/out.raw /tmp/out.png

	return 0;
}
