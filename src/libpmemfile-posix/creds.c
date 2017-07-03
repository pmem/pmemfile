/*
 * Copyright 2016-2017, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * creds.c -- pmemfile_* functions related to credentials
 */

#include <limits.h>

#include "creds.h"
#include "libpmemfile-posix.h"
#include "out.h"
#include "pool.h"

/*
 * pmemfile_setreuid -- sets real and effective user id
 */
int
pmemfile_setreuid(PMEMfilepool *pfp, pmemfile_uid_t ruid, pmemfile_uid_t euid)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (ruid != (pmemfile_uid_t)-1 && ruid > INT_MAX) {
		errno = EINVAL;
		return -1;
	}

	if (euid != (pmemfile_uid_t)-1 && euid > INT_MAX) {
		errno = EINVAL;
		return -1;
	}

	os_rwlock_wrlock(&pfp->cred_rwlock);
	if (ruid != (pmemfile_uid_t)-1)
		pfp->cred.ruid = ruid;
	if (euid != (pmemfile_uid_t)-1) {
		pfp->cred.euid = euid;
		pfp->cred.fsuid = euid;
	}
	os_rwlock_unlock(&pfp->cred_rwlock);

	return 0;
}

/*
 * pmemfile_setregid -- sets real and effective group id
 */
int
pmemfile_setregid(PMEMfilepool *pfp, pmemfile_gid_t rgid, pmemfile_gid_t egid)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (rgid != (pmemfile_gid_t)-1 && rgid > INT_MAX) {
		errno = EINVAL;
		return -1;
	}

	if (egid != (pmemfile_gid_t)-1 && egid > INT_MAX) {
		errno = EINVAL;
		return -1;
	}

	os_rwlock_wrlock(&pfp->cred_rwlock);
	if (rgid != (pmemfile_gid_t)-1)
		pfp->cred.rgid = rgid;
	if (egid != (pmemfile_gid_t)-1) {
		pfp->cred.egid = egid;
		pfp->cred.fsgid = egid;
	}
	os_rwlock_unlock(&pfp->cred_rwlock);

	return 0;
}

/*
 * pmemfile_setuid -- sets effective user id
 */
int
pmemfile_setuid(PMEMfilepool *pfp, pmemfile_uid_t uid)
{
	return pmemfile_setreuid(pfp, (pmemfile_uid_t)-1, uid);
}

/*
 * pmemfile_setgid -- sets effective group id
 */
int
pmemfile_setgid(PMEMfilepool *pfp, pmemfile_gid_t gid)
{
	return pmemfile_setregid(pfp, (pmemfile_gid_t)-1, gid);
}

/*
 * pmemfile_getuid -- returns real user id
 */
pmemfile_uid_t
pmemfile_getuid(PMEMfilepool *pfp)
{
	pmemfile_uid_t ret;
	os_rwlock_rdlock(&pfp->cred_rwlock);
	ret = pfp->cred.ruid;
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

/*
 * pmemfile_getgid -- returns real group id
 */
pmemfile_gid_t
pmemfile_getgid(PMEMfilepool *pfp)
{
	pmemfile_gid_t ret;
	os_rwlock_rdlock(&pfp->cred_rwlock);
	ret = pfp->cred.rgid;
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

/*
 * pmemfile_seteuid -- sets effective user id
 */
int
pmemfile_seteuid(PMEMfilepool *pfp, pmemfile_uid_t uid)
{
	return pmemfile_setreuid(pfp, (pmemfile_uid_t)-1, uid);
}

/*
 * pmemfile_setegid -- sets effective group id
 */
int
pmemfile_setegid(PMEMfilepool *pfp, pmemfile_gid_t gid)
{
	return pmemfile_setregid(pfp, (pmemfile_gid_t)-1, gid);
}

/*
 * pmemfile_geteuid -- returns effective user id
 */
pmemfile_uid_t
pmemfile_geteuid(PMEMfilepool *pfp)
{
	pmemfile_uid_t ret;
	os_rwlock_rdlock(&pfp->cred_rwlock);
	ret = pfp->cred.euid;
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

/*
 * pmemfile_getegid -- returns effective group id
 */
pmemfile_gid_t
pmemfile_getegid(PMEMfilepool *pfp)
{
	pmemfile_gid_t ret;
	os_rwlock_rdlock(&pfp->cred_rwlock);
	ret = pfp->cred.egid;
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

/*
 * pmemfile_setfsuid -- sets filesystem user id
 */
int
pmemfile_setfsuid(PMEMfilepool *pfp, pmemfile_uid_t fsuid)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (fsuid > INT_MAX) {
		errno = EINVAL;
		return -1;
	}

	os_rwlock_wrlock(&pfp->cred_rwlock);
	pmemfile_uid_t prev_fsuid = pfp->cred.fsuid;
	pfp->cred.fsuid = fsuid;
	os_rwlock_unlock(&pfp->cred_rwlock);

	return (int)prev_fsuid;
}

/*
 * pmemfile_setfsgid -- sets filesystem group id
 */
int
pmemfile_setfsgid(PMEMfilepool *pfp, pmemfile_gid_t fsgid)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (fsgid > INT_MAX) {
		errno = EINVAL;
		return -1;
	}

	os_rwlock_wrlock(&pfp->cred_rwlock);
	pmemfile_uid_t prev_fsgid = pfp->cred.fsgid;
	pfp->cred.fsgid = fsgid;
	os_rwlock_unlock(&pfp->cred_rwlock);

	return (int)prev_fsgid;
}

/*
 * pmemfile_getgroups -- fills "list" with supplementary group ids
 */
int
pmemfile_getgroups(PMEMfilepool *pfp, int size, pmemfile_gid_t list[])
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	if (size < 0) {
		errno = EINVAL;
		return -1;
	}

	os_rwlock_rdlock(&pfp->cred_rwlock);
	size_t groupsnum = pfp->cred.groupsnum;
	if (groupsnum > (size_t)size) {
		errno = EINVAL;
		os_rwlock_unlock(&pfp->cred_rwlock);
		return -1;
	}

	memcpy(list, pfp->cred.groups,
			pfp->cred.groupsnum * sizeof(pfp->cred.groups[0]));

	os_rwlock_unlock(&pfp->cred_rwlock);
	return (int)groupsnum;
}

/*
 * pmemfile_getgroups -- sets supplementary group ids
 */
int
pmemfile_setgroups(PMEMfilepool *pfp, size_t size, const pmemfile_gid_t *list)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	int error = 0;
	os_rwlock_wrlock(&pfp->cred_rwlock);
	if (size != pfp->cred.groupsnum) {
		void *r = realloc(pfp->cred.groups,
				size * sizeof(pfp->cred.groups[0]));
		if (!r) {
			error = errno;
			goto end;
		}

		pfp->cred.groups = r;
		pfp->cred.groupsnum = size;
	}
	memcpy(pfp->cred.groups, list, size * sizeof(*list));

end:
	os_rwlock_unlock(&pfp->cred_rwlock);

	if (error) {
		errno = error;
		return -1;
	}
	return 0;
}

/*
 * pmemfile_setcap -- sets current user capability
 */
int
pmemfile_setcap(PMEMfilepool *pfp, int cap)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	int ret = 0;
	os_rwlock_wrlock(&pfp->cred_rwlock);
	switch (cap) {
		case PMEMFILE_CAP_CHOWN:
		case PMEMFILE_CAP_FOWNER:
		case PMEMFILE_CAP_FSETID:
			pfp->cred.caps |= 1 << cap;
			break;
		default:
			errno = EINVAL;
			ret = -1;
			break;
	}
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

/*
 * pmemfile_clrcap -- clears current user capability
 */
int
pmemfile_clrcap(PMEMfilepool *pfp, int cap)
{
	if (!pfp) {
		LOG(LUSR, "NULL pool");
		errno = EFAULT;
		return -1;
	}

	int ret = 0;
	os_rwlock_wrlock(&pfp->cred_rwlock);
	switch (cap) {
		case PMEMFILE_CAP_CHOWN:
		case PMEMFILE_CAP_FOWNER:
		case PMEMFILE_CAP_FSETID:
			pfp->cred.caps &= ~(1 << cap);
			break;
		default:
			errno = EINVAL;
			ret = -1;
			break;
	}
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

/*
 * copy_cred -- copies credentials
 */
static int
copy_cred(struct pmemfile_cred *dst_cred, const struct pmemfile_cred *src_cred)
{
	dst_cred->ruid = src_cred->ruid;
	dst_cred->rgid = src_cred->rgid;

	dst_cred->euid = src_cred->euid;
	dst_cred->egid = src_cred->egid;

	dst_cred->fsuid = src_cred->fsuid;
	dst_cred->fsgid = src_cred->fsgid;

	dst_cred->caps = src_cred->caps;
	dst_cred->groupsnum = src_cred->groupsnum;
	if (dst_cred->groupsnum) {
		dst_cred->groups = malloc(dst_cred->groupsnum *
				sizeof(dst_cred->groups[0]));
		if (!dst_cred->groups)
			return -1;
		memcpy(dst_cred->groups, src_cred->groups, dst_cred->groupsnum *
				sizeof(dst_cred->groups[0]));
	} else {
		dst_cred->groups = NULL;
	}

	return 0;
}

/*
 * cred_acquire -- gets current credentials in a safe way
 */
int
cred_acquire(PMEMfilepool *pfp, struct pmemfile_cred *cred)
{
	int ret;
	os_rwlock_rdlock(&pfp->cred_rwlock);
	ret = copy_cred(cred, &pfp->cred);
	os_rwlock_unlock(&pfp->cred_rwlock);
	return ret;
}

/*
 * cred_release -- frees credentials obtained with "get_cred"
 */
void
cred_release(struct pmemfile_cred *cred)
{
	free(cred->groups);
	memset(cred, 0, sizeof(*cred));
}

/*
 * gid_in_list -- return true when gid is in supplementary groups list
 */
bool
gid_in_list(const struct pmemfile_cred *cred, pmemfile_gid_t gid)
{
	for (size_t i = 0; i < cred->groupsnum; ++i) {
		if (cred->groups[i] == gid)
			return true;
	}

	return false;
}

/*
 * can_access -- answers question: "can I access this inode with these
 * credentials to do specified action?"
 */
bool
can_access(const struct pmemfile_cred *cred, struct inode_perms perms, int acc)
{
	pmemfile_mode_t perm = perms.flags & PMEMFILE_ACCESSPERMS;
	pmemfile_mode_t req = 0;
	pmemfile_uid_t uid;
	pmemfile_gid_t gid;
	int acctype = acc & PFILE_ACCESS_MASK;

	if (acctype == PFILE_USE_FACCESS) {
		uid = cred->fsuid;
		gid = cred->fsgid;
	} else if (acctype == PFILE_USE_EACCESS) {
		uid = cred->euid;
		gid = cred->egid;
	} else if (acctype == PFILE_USE_RACCESS) {
		uid = cred->ruid;
		gid = cred->rgid;
	} else {
		return false;
	}

	if (perms.uid == uid) {
		if (acc & PFILE_WANT_READ)
			req |=  PMEMFILE_S_IRUSR;
		if (acc & PFILE_WANT_WRITE)
			req |=  PMEMFILE_S_IWUSR;
		if (acc & PFILE_WANT_EXECUTE)
			req |=  PMEMFILE_S_IXUSR;
	} else if (perms.gid == gid || gid_in_list(cred, perms.gid)) {
		if (acc & PFILE_WANT_READ)
			req |=  PMEMFILE_S_IRGRP;
		if (acc & PFILE_WANT_WRITE)
			req |=  PMEMFILE_S_IWGRP;
		if (acc & PFILE_WANT_EXECUTE)
			req |=  PMEMFILE_S_IXGRP;
	} else {
		if (acc & PFILE_WANT_READ)
			req |=  PMEMFILE_S_IROTH;
		if (acc & PFILE_WANT_WRITE)
			req |=  PMEMFILE_S_IWOTH;
		if (acc & PFILE_WANT_EXECUTE)
			req |=  PMEMFILE_S_IXOTH;
	}

	return ((perm & req) == req);
}

/*
 * vinode_can_access -- wrapper around "can_access" that deals with locked
 * vinode
 */
bool
_vinode_can_access(const struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode, int acc)
{
	struct inode_perms inode_perms = _vinode_get_perms(vinode);

	return can_access(cred, inode_perms, acc);
}

/*
 * vinode_can_access -- wrapper around "can_access" that deals with unlocked
 * vinode
 */
bool
vinode_can_access(const struct pmemfile_cred *cred,
		struct pmemfile_vinode *vinode, int acc)
{
	struct inode_perms inode_perms = vinode_get_perms(vinode);

	return can_access(cred, inode_perms, acc);
}
