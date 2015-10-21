#include "xjob.h"

/* hash usage borrowed from ecryptfs */
int checksum(int algo, const char *infile, const char *outfile, int oflags)
{
	struct scatterlist sg;
	struct crypto_hash *tfm;
	struct hash_desc desc;
	struct file *src = NULL;
	struct file *dst = NULL;
	u8 *buf = NULL;
	u8 *hash = NULL;
	char *digest = NULL;
	int bytes;
	int err = 0;
	int i;
	mm_segment_t old_fs;

	if (algo != ALGORITHM_MD5 && algo != ALGORITHM_SHA1) {
		err = -EINVAL;
		INFO("Invalid algorithm for checksum");
		goto out;
	}

	/* open files */
	src = filp_open(infile, O_RDONLY, 0);
	if (IS_ERR(src)) {
		INFO("Error opening infile '%s'", infile);
		err = PTR_ERR(src);
		goto out;
	}
	dst = filp_open(outfile, oflags | O_CREAT | O_TRUNC | O_WRONLY, 0644);
	if (IS_ERR(dst)) {
		INFO("Error opening outfile '%s'", outfile);
		err = PTR_ERR(dst);
		goto out;
	}

	/* init hash */
	tfm = crypto_alloc_hash(get_algo_name(algo), 0, CRYPTO_ALG_ASYNC);
	if (IS_ERR(tfm)) {
		err = PTR_ERR(tfm);
		INFO("Error allocating md5 hash; err = %d", err);
		goto out;
	}
	desc.tfm = tfm;
	desc.flags = CRYPTO_TFM_REQ_MAY_SLEEP;

	err = crypto_hash_init(&desc);
	if (err) {
		INFO("Error initializing crypto hash; err = %d", err);
		goto out_tfm;
	}

	buf = kmalloc(PAGE_SIZE, GFP_KERNEL);
	if (!buf) {
		err = -ENOMEM;
		goto out_tfm;
	}

	sg_init_one(&sg, buf, PAGE_SIZE);
	src->f_pos = 0;

	/* main loop */
	old_fs = get_fs();
	set_fs(get_ds());
	do {
		bytes = src->f_op->read(src, (__user u8 *)buf,
				PAGE_SIZE, &src->f_pos);
		if (bytes < 0) /* empty content can be hashed as well */
			break;

		err = crypto_hash_update(&desc, &sg, bytes);
		if (err) {
			INFO("Error updating crypto hash; err = %d", err);
			break;
		}
	} while (bytes > 0);
	set_fs(old_fs);

	if (bytes < 0) {
		err = bytes;
		goto out_tfm;
	}
	if (err)
		goto out_tfm;

	/* finalize */
	hash = kmalloc(get_hash_size(algo), GFP_KERNEL);
	if (!hash) {
		err = -ENOMEM;
		goto out_tfm;
	}
	digest = kmalloc(get_digest_size(algo) + 1, GFP_KERNEL);
	if (!digest) {
		err = -ENOMEM;
		goto out_tfm;
	}

	err = crypto_hash_final(&desc, hash);
	if (err) {
		INFO("Error finalizing crypto hash; err = %d", err);
		goto out_tfm;
	}

	for (i = 0; i < get_hash_size(algo); i++)
		sprintf(digest + 2 * i, "%02x", hash[i]);
	INFO("%.*s", get_digest_size(algo), digest);

	old_fs = get_fs();
	set_fs(get_ds());
	bytes = dst->f_op->write(dst, (__user char *)digest,
			get_digest_size(algo), &dst->f_pos);
	set_fs(old_fs);
	if (bytes < 0)
		err = bytes;
	else if (bytes != get_digest_size(algo))
		err = -EIO;

out_tfm:
	crypto_free_hash(tfm);
out:
	if (src && !IS_ERR(src))
		filp_close(src, NULL);
	if (dst && !IS_ERR(dst))
		filp_close(dst, NULL);
	kfree(buf);
	kfree(hash);
	kfree(digest);

	return err;
}
