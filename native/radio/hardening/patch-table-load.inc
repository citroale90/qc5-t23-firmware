/* SPDX-License-Identifier: GPL-2.0-only
 * Candidate replacement for the R43 Linux loader, not radio firmware code.
 * Preserve the 128-byte metadata upload and ordered address/value protocol.
 */
int aicwf_patch_table_load(struct rwnx_hw *rwnx_hw, char *filename)
{
    u32 *dst = NULL;
    int size, err;
    unsigned int i;

    size = rwnx_request_firmware_common(rwnx_hw, &dst, filename);
    if (!dst)
        return size < 0 ? size : -ENOENT;
    if (size < 128 || (size - 128) % 8) {
        err = -EINVAL;
        goto out;
    }

    err = rwnx_send_dbg_mem_block_write_req(rwnx_hw, dst[0], 128, dst);
    if (err)
        goto out;
    /* Bounded logging: metadata is not guaranteed to have a final NUL. */
    AICWFDBG(LOGINFO, "%.*s", 124, (char *)&dst[1]);

    for (i = 32; i < (unsigned int)size / 4; i += 2) {
        err = rwnx_send_dbg_mem_write_req(rwnx_hw, dst[i], dst[i + 1]);
        if (err)
            goto out;
    }
out:
    rwnx_release_firmware_common(&dst);
    return err;
}
