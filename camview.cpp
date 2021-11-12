#include "camview.h"




int cam_allocate_xbuf(viod *vd)

{
  int iVar1;
  uchar *puVar2;
  
  puVar2 = (uchar *)malloc((ulong)vd->buffer_maxsize);
  vd->xbuf = puVar2;
  if (vd->xbuf == (uchar *)0x0) {
    iVar1 = -1;
  }
  else {
    iVar1 = 0;
  }
  return iVar1;
}

int cam_deallocate_xbuf(viod *vd)

{
  if (vd->xbuf != (uchar *)0x0) {
    free(vd->xbuf);
    vd->xbuf = (uchar *)0x0;
  }
  return 0;
}

int cam_def_buffer_maxsize(viod *vd)

{
  long lVar1;
  int iVar2;
  long in_FS_OFFSET;
  v4l2_format fmt;
  
  lVar1 = *(long *)(in_FS_OFFSET + 0x28);
  memset(&fmt,0,0xd0);
  fmt.type = 1;
  iVar2 = xioctl(vd->fid,0xc0d05604,&fmt);
  if (iVar2 == -1) {
    puts("error reading format features");
    iVar2 = -1;
  }
  else {
    vd->buffer_maxsize = fmt.fmt._20_4_;
    iVar2 = 0;
  }
  if (lVar1 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return iVar2;
}

int cam_init_device(viod *vd)

{
  io_method iVar1;
  int iVar2;
  
  iVar1 = vd->io;
  if (iVar1 == IO_METHOD_DMABUF) {
    iVar2 = cam_init_dma(vd);
    return iVar2;
  }
  if ((int)iVar1 < 4) {
    if (iVar1 == IO_METHOD_MMAP) {
      iVar2 = cam_init_mmap(vd);
      return iVar2;
    }
    if ((int)iVar1 < 3) {
      if (iVar1 == IO_METHOD_READ) {
        iVar2 = cam_init_read(vd);
        return iVar2;
      }
      if (iVar1 == IO_METHOD_USERPTR) {
        iVar2 = cam_init_userp(vd);
        return iVar2;
      }
    }
  }
  return -1;
}

int cam_init_dma(viod *vd)

{
  return -1;
}

int cam_init_mmap(viod *vd)

{
  uint uVar1;
  long lVar2;
  bool bVar3;
  int iVar4;
  int *piVar5;
  vbuff *pvVar6;
  void *pvVar7;
  long in_FS_OFFSET;
  int er;
  v4l2_requestbuffers req;
  v4l2_buffer buf;
  
  lVar2 = *(long *)(in_FS_OFFSET + 0x28);
  memset(&req,0,0x14);
  req.count = 4;
  req.type = 1;
  req.memory = 1;
  iVar4 = xioctl(vd->fid,0xc0145608,&req);
  if (iVar4 == -1) {
    piVar5 = __errno_location();
    if (*piVar5 == 0x16) {
      fwrite("Device does not support memory mapping\n",1,0x27,stderr);
    }
    else {
      puts("error in memory buffer allocation VIDIOC_REQBUFS");
    }
    iVar4 = -1;
  }
  else {
    if (req.count < 2) {
      free_v4l2_video_buffers(vd);
      fwrite("Insufficient buffer memory on device\n",1,0x25,stderr);
      iVar4 = -1;
    }
    else {
      pvVar6 = (vbuff *)calloc((ulong)req.count,0x68);
      vd->buffers = pvVar6;
      if (vd->buffers == (vbuff *)0x0) {
        fwrite("Out of memory\n",1,0xe,stderr);
        iVar4 = -1;
      }
      else {
        bVar3 = false;
        vd->num_buf = 0;
        while (vd->num_buf < req.count) {
          memset(&buf,0,0x58);
          buf.type = 1;
          buf.memory = 1;
          buf.index = vd->num_buf;
          iVar4 = xioctl(vd->fid,0xc0585609,&buf);
          if (iVar4 == -1) {
            puts("error reading memory buffer features VIDIOC_QUERYBUF");
            bVar3 = true;
          }
          vd->buffers[vd->num_buf].length = (ulong)buf.length;
          pvVar6 = vd->buffers;
          uVar1 = vd->num_buf;
          pvVar7 = mmap((void *)0x0,(ulong)buf.length,3,1,vd->fid,(ulong)(uint)buf.m);
          pvVar6[uVar1].start = pvVar7;
          if ((vd->buffers[vd->num_buf].start == (void *)0xffffffffffffffff) || (bVar3)) {
            fwrite("Out of memory\n",1,0xe,stderr);
            vd->num_buf = vd->num_buf - 1;
            do {
              iVar4 = munmap(vd->buffers[vd->num_buf].start,vd->buffers[vd->num_buf].length);
              if (iVar4 == -1) {
                puts("error when unmapping memory");
              }
              vd->num_buf = vd->num_buf - 1;
            } while( true );
          }
          if (vd->buffer_maxsize < buf.length) {
            vd->buffer_maxsize = buf.length;
          }
          vd->num_buf = vd->num_buf + 1;
        }
        iVar4 = 0;
      }
    }
  }
  if (lVar2 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return iVar4;
}

int cam_init_read(viod *vd)

{
  int iVar1;
  vbuff *pvVar2;
  void *pvVar3;
  
  pvVar2 = (vbuff *)calloc(1,0x68);
  vd->buffers = pvVar2;
  if (vd->buffers == (vbuff *)0x0) {
    fwrite("Out of memory\n",1,0xe,stderr);
    iVar1 = -1;
  }
  else {
    vd->buffers->length = (ulong)vd->buffer_maxsize;
    pvVar2 = vd->buffers;
    pvVar3 = malloc((ulong)vd->buffer_maxsize);
    pvVar2->start = pvVar3;
    if (vd->buffers->start == (void *)0x0) {
      fwrite("Out of memory\n",1,0xe,stderr);
      iVar1 = -1;
    }
    else {
      iVar1 = 0;
    }
  }
  return iVar1;
}

int cam_init_userp(viod *vd)

{
  uint uVar1;
  long lVar2;
  int iVar3;
  int *piVar4;
  vbuff *pvVar5;
  void *pvVar6;
  long in_FS_OFFSET;
  v4l2_requestbuffers req;
  
  lVar2 = *(long *)(in_FS_OFFSET + 0x28);
  memset(&req,0,0x14);
  req.count = 4;
  req.type = 1;
  req.memory = 2;
  iVar3 = xioctl(vd->fid,0xc0145608,&req);
  if (iVar3 == -1) {
    piVar4 = __errno_location();
    if (*piVar4 == 0x16) {
      fwrite("Device does not support user pointer i/o\n",1,0x29,stderr);
    }
    else {
      puts("error in memory buffer allocation VIDIOC_REQBUFS");
    }
    iVar3 = -1;
  }
  else {
    pvVar5 = (vbuff *)calloc(4,0x68);
    vd->buffers = pvVar5;
    if (vd->buffers == (vbuff *)0x0) {
      fwrite("Out of memory\n",1,0xe,stderr);
      iVar3 = -1;
    }
    else {
      vd->num_buf = 0;
      while (vd->num_buf < req.count) {
        vd->buffers[vd->num_buf].length = (ulong)vd->buffer_maxsize;
        pvVar5 = vd->buffers;
        uVar1 = vd->num_buf;
        pvVar6 = malloc((ulong)vd->buffer_maxsize);
        pvVar5[uVar1].start = pvVar6;
        if (vd->buffers[vd->num_buf].start == (void *)0x0) {
          fwrite("Out of memory\n",1,0xe,stderr);
          vd->num_buf = vd->num_buf - 1;
          do {
            free(vd->buffers[vd->num_buf].start);
            vd->num_buf = vd->num_buf - 1;
          } while( true );
        }
        vd->num_buf = vd->num_buf + 1;
      }
      iVar3 = 0;
    }
  }
  if (lVar2 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return iVar3;
}


int cam_mainloop(viod *vd)

{
  byte bVar1;
  int iVar2;
  int *piVar3;
  long lVar4;
  fd_set *pfVar5;
  long in_FS_OFFSET;
  int r;
  int __d0;
  int __d1;
  timeval tv;
  fd_set fds;
  long local_10;
  
  local_10 = *(long *)(in_FS_OFFSET + 0x28);
  while( true ) {
    if (vd->thon != 1) {
      iVar2 = 0;
      goto LAB_001021bc;
    }
    while( true ) {
      pfVar5 = &fds;
      for (lVar4 = 0x10; lVar4 != 0; lVar4 = lVar4 + -1) {
        pfVar5->fds_bits[0] = 0;
        pfVar5 = (fd_set *)(pfVar5->fds_bits + 1);
      }
      iVar2 = vd->fid;
      if (iVar2 < 0) {
        iVar2 = iVar2 + 0x3f;
      }
      bVar1 = (byte)(vd->fid >> 0x37);
      fds.fds_bits[iVar2 >> 6] =
           fds.fds_bits[iVar2 >> 6] |
           1 << (((char)vd->fid + (bVar1 >> 2) & 0x3f) - (bVar1 >> 2) & 0x3f);
      tv.tv_sec = 5;
      tv.tv_usec = 0;
      iVar2 = select(vd->fid + 1,(fd_set *)&fds,(fd_set *)0x0,(fd_set *)0x0,(timeval *)&tv);
      if (iVar2 != -1) break;
      piVar3 = __errno_location();
      if (*piVar3 != 4) {
        iVar2 = -2;
        goto LAB_001021bc;
      }
    }
    if (iVar2 == 0) break;
    iVar2 = cam_read_frame(vd);
    if (iVar2 != 0) {
      iVar2 = -2;
LAB_001021bc:
      if (local_10 == *(long *)(in_FS_OFFSET + 0x28)) {
        return iVar2;
      }
                    /* WARNING: Subroutine does not return */
      __stack_chk_fail();
    }
  }
  fwrite("select timeout\n",1,0xf,stderr);
  iVar2 = -2;
  goto LAB_001021bc;
  
}

int cam_read_frame(viod *vd)

{
  io_method iVar1;
  long lVar2;
  int iVar3;
  ssize_t sVar4;
  int *piVar5;
  vbuff *pvVar6;
  long in_FS_OFFSET;
  uint i;
  v4l2_buffer buf;
  
  lVar2 = *(long *)(in_FS_OFFSET + 0x28);
  iVar1 = vd->io;
  if (iVar1 == IO_METHOD_DMABUF) {
    iVar3 = -2;
    goto LAB_00102019;
  }
  if ((int)iVar1 < 4) {
    if (iVar1 == IO_METHOD_MMAP) {
      memset(&buf,0,0x58);
      buf.type = 1;
      buf.memory = 1;
      iVar3 = xioctl(vd->fid,0xc0585611,&buf);
      if (iVar3 == -1) {
        piVar5 = __errno_location();
        if (*piVar5 == 0xb) {
          puts("error found");
          iVar3 = -2;
        }
        else {
          iVar3 = -2;
        }
      }
      else {
        if (vd->num_buf <= buf.index) {
                    /* WARNING: Subroutine does not return */
          __assert_fail("buf.index < vd.num_buf",
                        "/home/josemar/onedrive/projects/basecode/camview.cpp",0x380,
                        "int cam_read_frame(viod&)");
        }
        vd->bon = buf.index;
        pvVar6 = vd->buffers + vd->bon;
        *(ulong *)&pvVar6->binf = CONCAT44(buf.type,buf.index);
        *(ulong *)&(pvVar6->binf).bytesused = CONCAT44(buf.flags,buf.bytesused);
        *(undefined8 *)&(pvVar6->binf).field = buf._16_8_;
        (pvVar6->binf).timestamp.tv_sec = buf.timestamp.tv_sec;
        (pvVar6->binf).timestamp.tv_usec = buf.timestamp.tv_usec;
        *(undefined8 *)&(pvVar6->binf).timecode = buf.timecode._0_8_;
        *(undefined8 *)&(pvVar6->binf).timecode.frames = buf.timecode._8_8_;
        *(ulong *)&(pvVar6->binf).sequence = CONCAT44(buf.memory,buf.sequence);
        (pvVar6->binf).m = buf.m;
        *(ulong *)&(pvVar6->binf).length = CONCAT44(buf.reserved2,buf.length);
        *(undefined8 *)&(pvVar6->binf).field_12 = buf._80_8_;
        if (vd->buffer_maxsize < buf.bytesused) {
          puts("erro size buffer");
        }
        cam_process_image((_video_io_device *)vd);
        iVar3 = xioctl(vd->fid,0xc058560f,&buf);
        if (iVar3 == -1) {
          puts("error found");
          iVar3 = -2;
        }
        else {
          iVar3 = 0;
        }
      }
      goto LAB_00102019;
    }
    if ((int)iVar1 < 3) {
      if (iVar1 == IO_METHOD_READ) {
        sVar4 = read(vd->fid,vd->buffers->start,vd->buffers->length);
        if (sVar4 == -1) {
          piVar5 = __errno_location();
          if (*piVar5 == 0xb) {
            iVar3 = -2;
          }
          else {
            errno_exit(&DAT_00103093);
            iVar3 = -2;
          }
        }
        else {
          vd->bon = 0;
          cam_process_image((_video_io_device *)vd);
          iVar3 = 0;
        }
        goto LAB_00102019;
      }
      if (iVar1 == IO_METHOD_USERPTR) {
        memset(&buf,0,0x58);
        buf.type = 1;
        buf.memory = 2;
        iVar3 = xioctl(vd->fid,0xc0585611,&buf);
        if (iVar3 == -1) {
          piVar5 = __errno_location();
          if (*piVar5 == 0xb) {
            iVar3 = -2;
          }
          else {
            iVar3 = -2;
          }
        }
        else {
          i = 0;
          while ((i < vd->num_buf &&
                 ((buf.m != vd->buffers[i].start || ((ulong)buf.length != vd->buffers[i].length)))))
          {
            i = i + 1;
          }
          if (vd->num_buf <= i) {
                    /* WARNING: Subroutine does not return */
            __assert_fail("i < vd.num_buf","/home/josemar/onedrive/projects/basecode/camview.cpp",
                          0x34c,"int cam_read_frame(viod&)");
          }
          vd->bon = i;
          pvVar6 = vd->buffers + vd->bon;
          *(ulong *)&pvVar6->binf = CONCAT44(buf.type,buf.index);
          *(ulong *)&(pvVar6->binf).bytesused = CONCAT44(buf.flags,buf.bytesused);
          *(undefined8 *)&(pvVar6->binf).field = buf._16_8_;
          (pvVar6->binf).timestamp.tv_sec = buf.timestamp.tv_sec;
          (pvVar6->binf).timestamp.tv_usec = buf.timestamp.tv_usec;
          *(undefined8 *)&(pvVar6->binf).timecode = buf.timecode._0_8_;
          *(undefined8 *)&(pvVar6->binf).timecode.frames = buf.timecode._8_8_;
          *(ulong *)&(pvVar6->binf).sequence = CONCAT44(buf.memory,buf.sequence);
          (pvVar6->binf).m = buf.m;
          *(ulong *)&(pvVar6->binf).length = CONCAT44(buf.reserved2,buf.length);
          *(undefined8 *)&(pvVar6->binf).field_12 = buf._80_8_;
          if (vd->buffer_maxsize < buf.bytesused) {
            puts("erro size buffer");
          }
          cam_process_image((_video_io_device *)vd);
          iVar3 = xioctl(vd->fid,0xc058560f,&buf);
          if (iVar3 == -1) {
            puts("error VIDIOC_QBUF");
            iVar3 = -2;
          }
          else {
            iVar3 = 0;
          }
        }
        goto LAB_00102019;
      }
    }
  }
  iVar3 = -2;
LAB_00102019:
  if (lVar2 == *(long *)(in_FS_OFFSET + 0x28)) {
    return iVar3;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}

int cam_start_capturing(viod *vd)

{
  io_method iVar1;
  long lVar2;
  int iVar3;
  int *piVar4;
  long in_FS_OFFSET;
  v4l2_buf_type type;
  uint i;
  int er;
  v4l2_buffer buf;
  
  lVar2 = *(long *)(in_FS_OFFSET + 0x28);
  er = 0;
  iVar1 = vd->io;
  if (iVar1 == IO_METHOD_DMABUF) {
    iVar3 = -1;
    goto LAB_00101b43;
  }
  if ((int)iVar1 < 4) {
    if (iVar1 == IO_METHOD_MMAP) {
      for (i = 0; i < vd->num_buf; i = i + 1) {
        memset(&buf,0,0x58);
        buf.type = 1;
        buf.memory = 1;
        buf.index = i;
        iVar3 = xioctl(vd->fid,0xc058560f,&buf);
        if (iVar3 == -1) {
          puts("error in VIDIOC_QBUF");
          er = 1;
        }
      }
      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      iVar3 = xioctl(vd->fid,0x40045612,&type);
      if (iVar3 == -1) {
        puts("error starting stream VIDIOC_STREAMON");
        piVar4 = __errno_location();
        if (*piVar4 == 0x1c) {
          puts("There is no bandwidth enough. Try other formar, size or another controller");
        }
        er = 1;
      }
      if (er == 0) {
        iVar3 = 0;
      }
      else {
        cam_stop_capturing(vd);
        cam_uninit_device(vd);
        iVar3 = -1;
      }
      goto LAB_00101b43;
    }
    if ((int)iVar1 < 3) {
      if (iVar1 == IO_METHOD_READ) {
        iVar3 = 0;
        goto LAB_00101b43;
      }
      if (iVar1 == IO_METHOD_USERPTR) {
        for (i = 0; i < vd->num_buf; i = i + 1) {
          memset(&buf,0,0x58);
          buf.type = 1;
          buf.memory = 2;
          buf.index = i;
          buf.m = vd->buffers[i].start;
          buf.length = (__u32)vd->buffers[i].length;
          iVar3 = xioctl(vd->fid,0xc058560f,&buf);
          if (iVar3 == -1) {
            puts("error in VIDIOC_QBUF");
            er = 1;
          }
        }
        type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
        iVar3 = xioctl(vd->fid,0x40045612,&type);
        if (iVar3 == -1) {
          puts("error starting stream VIDIOC_STREAMON");
          puts("There is no bandwidth enough. Try other formar, size or another controller");
          er = 1;
        }
        if (er == 0) {
          iVar3 = 0;
        }
        else {
          cam_stop_capturing(vd);
          cam_uninit_device(vd);
          iVar3 = -1;
        }
        goto LAB_00101b43;
      }
    }
  }
  iVar3 = -1;
LAB_00101b43:
  if (lVar2 == *(long *)(in_FS_OFFSET + 0x28)) {
    return iVar3;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
}

int cam_stop_capturing(viod *vd)

{
  io_method iVar1;
  int iVar2;
  int *piVar3;
  long in_FS_OFFSET;
  v4l2_buf_type type;
  long local_10;
  
  local_10 = *(long *)(in_FS_OFFSET + 0x28);
  iVar1 = vd->io;
  if (iVar1 == IO_METHOD_DMABUF) {
    iVar2 = -2;
    goto LAB_001022b0;
  }
  if ((int)iVar1 < 4) {
    if (iVar1 == IO_METHOD_READ) {
      iVar2 = 0;
      goto LAB_001022b0;
    }
    if ((-1 < (int)iVar1) && (iVar1 + ~IO_METHOD_READ < 2)) {
      type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
      iVar2 = xioctl(vd->fid,0x40045613,&type);
      if (iVar2 == -1) {
        piVar3 = __errno_location();
        if (*piVar3 == 9) {
          iVar2 = -2;
        }
        else {
          piVar3 = __errno_location();
          if (*piVar3 == 0x13) {
            iVar2 = -2;
          }
          else {
            piVar3 = __errno_location();
            printf("error in stop stream VIDIOC_STREAMOFF - errno = %d\n",*piVar3);
            iVar2 = -2;
          }
        }
      }
      else {
        iVar2 = 0;
      }
      goto LAB_001022b0;
    }
  }
  iVar2 = -2;
LAB_001022b0:
  if (local_10 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return iVar2;
}

void * cam_thread_opencv(void *vd)

{
  viod *p_vd;
  
  *(undefined4 *)((long)vd + 0x4c) = 1;
  capture_pictures_opencv((viod *)vd);
  return (void *)0x0;
}


void * cam_thread_v4l2(void *vd)

{
  long in_FS_OFFSET;
  viod *p;
  Mat local_88;
  long local_20;
  
  local_20 = *(long *)(in_FS_OFFSET + 0x28);
  *(undefined4 *)((long)vd + 0x4c) = 1;
  *(undefined4 *)((long)vd + 0x18) = 0x10;
  gettimeofday((timeval *)((long)vd + 0x58),(__timezone_ptr_t)0x0);
  *(undefined8 *)((long)vd + 0x68) = *(undefined8 *)((long)vd + 0x58);
  *(undefined8 *)((long)vd + 0x70) = *(undefined8 *)((long)vd + 0x60);
  *(undefined8 *)((long)vd + 0x78) = *(undefined8 *)((long)vd + 0x58);
  *(undefined8 *)((long)vd + 0x80) = *(undefined8 *)((long)vd + 0x60);
  *(undefined8 *)((long)vd + 0x88) = *(undefined8 *)((long)vd + 0x58);
  *(undefined8 *)((long)vd + 0x90) = *(undefined8 *)((long)vd + 0x60);
  *(undefined8 *)((long)vd + 0x98) = *(undefined8 *)((long)vd + 0x58);
  *(undefined8 *)((long)vd + 0xa0) = *(undefined8 *)((long)vd + 0x60);
  *(undefined8 *)((long)vd + 0xa8) = *(undefined8 *)((long)vd + 0x58);
  *(undefined8 *)((long)vd + 0xb0) = *(undefined8 *)((long)vd + 0x60);
  *(undefined8 *)((long)vd + 0xb8) = *(undefined8 *)((long)vd + 0x58);
  *(undefined8 *)((long)vd + 0xc0) = *(undefined8 *)((long)vd + 0x60);
  *(undefined8 *)((long)vd + 200) = *(undefined8 *)((long)vd + 0x58);
  *(undefined8 *)((long)vd + 0xd0) = *(undefined8 *)((long)vd + 0x60);
  cv::Mat::Mat(&local_88,*(int *)((long)vd + 0x34),*(int *)((long)vd + 0x30),0x10);
                    /* try { // try from 0010276f to 00102773 has its CatchHandler @ 00102820 */
  std::vector<cv--Mat,std--allocator<cv--Mat>>::push_back
            ((vector<cv--Mat,std--allocator<cv--Mat>> *)((long)vd + 0xf8),(value_type *)&local_88);
  cv::Mat::~Mat(&local_88);
  capture_pictures_v4l2((viod *)vd);
  erase_process_initialization((viod *)vd);
  *(undefined4 *)((long)vd + 0x128) = 0;
  *(undefined8 *)((long)vd + 0x38) = 0;
  *(undefined4 *)((long)vd + 0x40) = 0;
  *(undefined4 *)((long)vd + 300) = 0;
  *(undefined8 *)vd = 0;
  *(undefined4 *)((long)vd + 0xdc) = 0;
  *(undefined4 *)((long)vd + 0x18) = 2;
  if (local_20 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return (void *)0x0;
}


int cam_uninit_device(viod *vd)

{
  io_method iVar1;
  int iVar2;
  uint i;
  
  iVar1 = vd->io;
  if (iVar1 == IO_METHOD_DMABUF) {
    return -2;
  }
  if ((int)iVar1 < 4) {
    if (iVar1 == IO_METHOD_MMAP) {
      for (i = 0; i < vd->num_buf; i = i + 1) {
        iVar2 = munmap(vd->buffers[i].start,vd->buffers[i].length);
        if (iVar2 == -1) {
          puts("error when unmapping memory");
          return -2;
        }
      }
      iVar2 = free_v4l2_video_buffers(vd);
      if (iVar2 == -1) {
        return -2;
      }
      if (vd->buffers != (vbuff *)0x0) {
        free(vd->buffers);
        vd->buffers = (vbuff *)0x0;
      }
      return 0;
    }
    if ((int)iVar1 < 3) {
      if (iVar1 == IO_METHOD_READ) {
        free(vd->buffers->start);
        if (vd->buffers != (vbuff *)0x0) {
          free(vd->buffers);
          vd->buffers = (vbuff *)0x0;
        }
        return 0;
      }
      if (iVar1 == IO_METHOD_USERPTR) {
        for (i = 0; i < vd->num_buf; i = i + 1) {
          free(vd->buffers[i].start);
          vd->buffers[i].start = (void *)0x0;
          vd->buffers[i].length = 0;
          memset(&vd->buffers[i].binf,0,0x58);
        }
        if (vd->buffers != (vbuff *)0x0) {
          free(vd->buffers);
          vd->buffers = (vbuff *)0x0;
        }
        return 0;
      }
    }
  }
  return -2;
}



int capture_pictures_opencv(viod *vd)

{
  long lVar1;
  vbuff *pvVar2;
  long in_FS_OFFSET;
  int r;
  VideoCapture cap;
  Mat frame;
  
  lVar1 = *(long *)(in_FS_OFFSET + 0x28);
  cv::Mat::Mat(&frame);
  vd->st = 0x10;
                    /* try { // try from 001029c2 to 001029c6 has its CatchHandler @ 00102b1c */
  cv::VideoCapture::VideoCapture(&cap,(uint)(vd->vid).num,0);
  pvVar2 = (vbuff *)calloc(1,0x68);
  vd->buffers = pvVar2;
  while (vd->thon != 0) {
                    /* try { // try from 00102a11 to 00102a8a has its CatchHandler @ 00102b04 */
    (*(code *)0x4880558d48c18948)(&cap,&frame,&frame,0x4880558d48c18948);
    vd->buffers->start = frame.data;
    vd->h = frame.rows;
    vd->w = frame.cols;
    vd->buffers->length = (long)(vd->h * vd->w * 3);
    vd->bon = 0;
    cam_process_image((_video_io_device *)vd);
  }
  if (vd->buffers != (vbuff *)0x0) {
    free(vd->buffers);
    vd->buffers = (vbuff *)0x0;
  }
  vd->st = 2;
  cv::VideoCapture::~VideoCapture(&cap);
  cv::Mat::~Mat(&frame);
  if (lVar1 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return 0;
}


int capture_pictures_v4l2(viod *vd)

{
  int r;
  
  r = cam_def_buffer_maxsize(vd);
  if (r == 0) {
    r = cam_init_device(vd);
  }
  if (r == 0) {
    r = cam_allocate_xbuf(vd);
  }
  if (r == 0) {
    r = cam_start_capturing(vd);
  }
  if (r == 0) {
    r = cam_mainloop(vd);
  }
  if ((r == 0) || (r == -2)) {
    r = cam_stop_capturing(vd);
  }
  if ((r == 0) || (r == -2)) {
    r = cam_uninit_device(vd);
  }
  if ((r == 0) || (r == -2)) {
    r = cam_deallocate_xbuf(vd);
  }
  return r;
}



void erase_process_initialization(viod *vd)

{
  size_type sVar1;
  reference pvVar2;
  long in_FS_OFFSET;
  iterator it;
  iterator local_40;
  const_iterator local_38;
  const_iterator local_30;
  void *p;
  long local_20;
  
  local_20 = *(long *)(in_FS_OFFSET + 0x28);
  it = (Mat *)0x0;
  it = (Mat *)std::vector<cv--Mat,std--allocator<cv--Mat>>::begin(&vd->v_mat);
  __gnu_cxx::__normal_iterator<cv--Mat*,std--vector<cv--Mat,std--allocator<cv--Mat>>>::operator++
            ((__normal_iterator<cv--Mat*,std--vector<cv--Mat,std--allocator<cv--Mat>>> *)&it,0);
  local_40 = (Mat *)std::vector<cv--Mat,std--allocator<cv--Mat>>::end(&vd->v_mat);
  __gnu_cxx::__normal_iterator<constcv--Mat*,std--vector<cv--Mat,std--allocator<cv--Mat>>>::
  __normal_iterator<cv--Mat*>
            ((__normal_iterator<constcv--Mat*,std--vector<cv--Mat,std--allocator<cv--Mat>>> *)
             &local_30,
             (__normal_iterator<cv--Mat*,std--vector<cv--Mat,std--allocator<cv--Mat>>> *)&local_40);
  __gnu_cxx::__normal_iterator<constcv--Mat*,std--vector<cv--Mat,std--allocator<cv--Mat>>>::
  __normal_iterator<cv--Mat*>
            ((__normal_iterator<constcv--Mat*,std--vector<cv--Mat,std--allocator<cv--Mat>>> *)
             &local_38,
             (__normal_iterator<cv--Mat*,std--vector<cv--Mat,std--allocator<cv--Mat>>> *)&it);
  std::vector<cv--Mat,std--allocator<cv--Mat>>::erase
            (&vd->v_mat,(const_iterator)local_38,(const_iterator)local_30);
  std::vector<cv--Mat,std--allocator<cv--Mat>>::clear(&vd->c_mat);
  while( true ) {
    sVar1 = std::vector<_dany,std--allocator<_dany>>::size(&vd->d_vet);
    if (sVar1 == 0) break;
    pvVar2 = std::vector<_dany,std--allocator<_dany>>::at(&vd->d_vet,0);
    p = pvVar2->pobj;
    pvVar2 = std::vector<_dany,std--allocator<_dany>>::at(&vd->d_vet,0);
    (*pvVar2->pdest)(p);
    local_38 = (Mat *)std::vector<_dany,std--allocator<_dany>>::begin(&vd->d_vet);
    __gnu_cxx::__normal_iterator<const_dany*,std--vector<_dany,std--allocator<_dany>>>::
    __normal_iterator<_dany*>
              ((__normal_iterator<const_dany*,std--vector<_dany,std--allocator<_dany>>> *)&local_30,
               (__normal_iterator<_dany*,std--vector<_dany,std--allocator<_dany>>> *)&local_38);
    std::vector<_dany,std--allocator<_dany>>::erase(&vd->d_vet,(const_iterator)local_30);
  }
  if (local_20 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


int free_v4l2_video_buffers(viod *vd)

{
  long lVar1;
  int iVar2;
  int *piVar3;
  long in_FS_OFFSET;
  v4l2_requestbuffers req;
  
  lVar1 = *(long *)(in_FS_OFFSET + 0x28);
  memset(&req,0,0x14);
  req.type = 1;
  req.memory = vd->io;
  req.count = 0;
  iVar2 = ioctl(vd->fid,0xc0145608,&req);
  if (iVar2 == -1) {
    piVar3 = __errno_location();
    if (*piVar3 != 9) {
      piVar3 = __errno_location();
      if (*piVar3 != 0x13) {
        piVar3 = __errno_location();
        if (*piVar3 != 0x10) {
          puts("error in check_io_method_busy function");
          iVar2 = -1;
          goto LAB_00100fed;
        }
      }
    }
  }
  iVar2 = 0;
LAB_00100fed:
  if (lVar1 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return iVar2;
}


int is_displaying(vector<_video_io_device*,std--allocator<_video_io_device*>> *vv)

{
  bool bVar1;
  int iVar2;
  reference pp_Var3;
  long in_FS_OFFSET;
  iterator it;
  iterator local_18;
  long local_10;
  
  local_10 = *(long *)(in_FS_OFFSET + 0x28);
  it = (_video_io_device **)0x0;
  it = (_video_io_device **)
       std::vector<_video_io_device*,std--allocator<_video_io_device*>>::begin(vv);
  do {
    local_18 = (_video_io_device **)
               std::vector<_video_io_device*,std--allocator<_video_io_device*>>::end(vv);
    bVar1 = __gnu_cxx::operator!=<_video_io_device**,std--vector<_video_io_device*>>
                      ((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                        *)&it,(__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                               *)&local_18);
    if (bVar1 == false) {
      iVar2 = 0;
LAB_00100b07:
      if (local_10 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
        __stack_chk_fail();
      }
      return iVar2;
    }
    pp_Var3 = __gnu_cxx::
              __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
              ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                           *)&it);
    if ((*pp_Var3)->view == 1) {
      iVar2 = 1;
      goto LAB_00100b07;
    }
    __gnu_cxx::
    __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
    ::operator++((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                  *)&it,0);
  } while( true );
}


int make_camera_list(vector<_video_io_device*,std--allocator<_video_io_device*>> *vv)

{
  long lVar1;
  _video_io_device *p_Var2;
  bool bVar3;
  int iVar4;
  int iVar5;
  io_method iVar6;
  reference pp_Var7;
  long in_FS_OFFSET;
  int i;
  int cnt;
  iterator it;
  iterator local_b8;
  viod *vp;
  char ck [127];
  
  lVar1 = *(long *)(in_FS_OFFSET + 0x28);
  memset(ck,0,0x7f);
  iVar4 = make_video_list();
  bVar3 = std::vector<_video_io_device*,std--allocator<_video_io_device*>>::empty(vv);
  if (bVar3 == false) {
    it = (_video_io_device **)0x0;
    it = (_video_io_device **)
         std::vector<_video_io_device*,std--allocator<_video_io_device*>>::begin(vv);
    while( true ) {
      local_b8 = (_video_io_device **)
                 std::vector<_video_io_device*,std--allocator<_video_io_device*>>::end(vv);
      bVar3 = __gnu_cxx::operator!=<_video_io_device**,std--vector<_video_io_device*>>
                        ((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                          *)&it,(__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                 *)&local_b8);
      if (bVar3 == false) break;
      pp_Var7 = __gnu_cxx::
                __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                             *)&it);
      iVar5 = found_in_list((*pp_Var7)->vid,ck);
      if (iVar5 == 0) {
        pp_Var7 = __gnu_cxx::
                  __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                  ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                               *)&it);
        vp = (viod *)*pp_Var7;
        ((_video_io_device *)vp)->view = 0;
        ((_video_io_device *)vp)->thon = 0;
        while (pp_Var7 = __gnu_cxx::
                         __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                         ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                      *)&it), 4 < (*pp_Var7)->st) {
          sleep(1);
        }
        if (vp->xbuf != (uchar *)0x0) {
          cam_deallocate_xbuf(vp);
        }
        if (vp->buffers != (vbuff *)0x0) {
          free(vp->buffers);
          vp->buffers = (vbuff *)0x0;
        }
        close_v4l2_device(vp->fid);
        pp_Var7 = __gnu_cxx::
                  __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                  ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                               *)&it);
        p_Var2 = *pp_Var7;
        if (p_Var2 != (_video_io_device *)0x0) {
          _video_io_device::~_video_io_device(p_Var2);
          operator.delete(p_Var2);
        }
        __gnu_cxx::
        __normal_iterator<_video_io_device*const*,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
        ::__normal_iterator<_video_io_device**>
                  ((__normal_iterator<_video_io_device*const*,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                    *)&local_b8,
                   (__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                    *)&it);
        std::vector<_video_io_device*,std--allocator<_video_io_device*>>::erase
                  (vv,(const_iterator)local_b8);
      }
      else {
        pp_Var7 = __gnu_cxx::
                  __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                  ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                               *)&it);
        if ((*pp_Var7)->st < 0x21) {
          switch((*pp_Var7)->st) {
          case 1:
            pp_Var7 = __gnu_cxx::
                      __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                      ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                   *)&it);
            iVar5 = (*pp_Var7)->fid;
            pp_Var7 = __gnu_cxx::
                      __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                      ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                   *)&it);
            p_Var2 = *pp_Var7;
            iVar6 = check_io_method_busy(iVar5);
            p_Var2->io = iVar6;
            pp_Var7 = __gnu_cxx::
                      __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                      ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                   *)&it);
            if ((*pp_Var7)->io == ~IO_METHOD_READ) {
              pp_Var7 = __gnu_cxx::
                        __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                        ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                     *)&it);
              (*pp_Var7)->st = 1;
              pp_Var7 = __gnu_cxx::
                        __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                        ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                     *)&it);
              update_cam_format((viod *)*pp_Var7);
            }
            else {
              pp_Var7 = __gnu_cxx::
                        __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                        ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                     *)&it);
              (*pp_Var7)->st = 2;
            }
            break;
          case 2:
            pp_Var7 = __gnu_cxx::
                      __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                      ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                   *)&it);
            iVar5 = (*pp_Var7)->fid;
            pp_Var7 = __gnu_cxx::
                      __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                      ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                   *)&it);
            p_Var2 = *pp_Var7;
            iVar6 = check_io_method_busy(iVar5);
            p_Var2->io = iVar6;
            pp_Var7 = __gnu_cxx::
                      __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                      ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                   *)&it);
            if ((*pp_Var7)->io == ~IO_METHOD_READ) {
              pp_Var7 = __gnu_cxx::
                        __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                        ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                     *)&it);
              (*pp_Var7)->st = 1;
              pp_Var7 = __gnu_cxx::
                        __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                        ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                     *)&it);
              update_cam_format((viod *)*pp_Var7);
            }
            else {
              pp_Var7 = __gnu_cxx::
                        __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                        ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                     *)&it);
              (*pp_Var7)->st = 2;
            }
            break;
          case 4:
            pp_Var7 = __gnu_cxx::
                      __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                      ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                   *)&it);
            iVar5 = (*pp_Var7)->fid;
            pp_Var7 = __gnu_cxx::
                      __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                      ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                   *)&it);
            p_Var2 = *pp_Var7;
            iVar6 = check_io_method_busy(iVar5);
            p_Var2->io = iVar6;
            pp_Var7 = __gnu_cxx::
                      __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                      ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                   *)&it);
            if ((*pp_Var7)->io == ~IO_METHOD_READ) {
              pp_Var7 = __gnu_cxx::
                        __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                        ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                     *)&it);
              (*pp_Var7)->st = 1;
            }
            else {
              pp_Var7 = __gnu_cxx::
                        __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                        ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                     *)&it);
              (*pp_Var7)->st = 4;
            }
          }
        }
        __gnu_cxx::
        __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
        ::operator++((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                      *)&it,0);
      }
    }
    for (i = 0; i < iVar4; i = i + 1) {
      if (ck[i] == '\0') {
        add_camera(vv,i);
      }
    }
  }
  else {
    for (i = 0; i < iVar4; i = i + 1) {
      add_camera(vv,i);
    }
  }
  if (lVar1 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return 0;
}


int setting_camera_features(viod *vd)

{
  long lVar1;
  long in_FS_OFFSET;
  v4l2_frmivalenum fl;
  
  lVar1 = *(long *)(in_FS_OFFSET + 0x28);
  if (vd->fid < 0) {
                    /* WARNING: Subroutine does not return */
    exit(1);
  }
  choose_or_enumerate_image_formats(vd->fid,&fl,0);
  choose_or_enumerate_frame_size(vd->fid,&fl,0);
  choose_or_enumerate_frame_intervals(vd->fid,&fl,0);
  set_image_format(vd->fid,fl.pixel_format);
  set_image_size(vd->fid,fl.width,fl.height);
  set_image_intervals(vd->fid,fl.field_5._0_4_,fl.field_5._4_4_);
  vd->w = fl.width;
  vd->h = fl.height;
  vd->pxfmt = fl.pixel_format;
  free_v4l2_video_buffers(vd);
  vd->st = 4;
  if (lVar1 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return 0;
}



int show_camera_list(vector<_video_io_device*,std--allocator<_video_io_device*>> *vv,int dvst)

{
  byte bVar1;
  long lVar2;
  char *param5;
  bool bVar3;
  int iVar4;
  size_type sVar5;
  reference pp_Var6;
  long in_FS_OFFSET;
  int i;
  int cnt;
  int err;
  iterator it;
  iterator local_1a0;
  v4l2_capability vcap;
  char name [256];
  
  lVar2 = *(long *)(in_FS_OFFSET + 0x28);
  i = 0;
  cnt = 0;
  printf("\x1b[H\x1b[J");
  sVar5 = std::vector<_video_io_device*,std--allocator<_video_io_device*>>::size(vv);
  if (sVar5 == 0) {
    puts("\tThere is no video device to show.\n");
    cnt = 0;
  }
  else {
    puts("\n\nVideo Cameras List:\n");
    it = (_video_io_device **)0x0;
    it = (_video_io_device **)
         std::vector<_video_io_device*,std--allocator<_video_io_device*>>::begin(vv);
LAB_0010087b:
    local_1a0 = (_video_io_device **)
                std::vector<_video_io_device*,std--allocator<_video_io_device*>>::end(vv);
    bVar3 = __gnu_cxx::operator!=<_video_io_device**,std--vector<_video_io_device*>>
                      ((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                        *)&it,(__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                               *)&local_1a0);
    if (bVar3 != false) {
      if ((dvst == 0x40) ||
         (pp_Var6 = __gnu_cxx::
                    __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                    ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                                 *)&it), ((*pp_Var6)->st & dvst) != 0)) {
        bVar3 = true;
      }
      else {
        bVar3 = false;
      }
      if (bVar3) goto code_r0x001008ee;
      goto LAB_00100a1a;
    }
    putchar(10);
  }
  if (lVar2 == *(long *)(in_FS_OFFSET + 0x28)) {
    return cnt;
  }
                    /* WARNING: Subroutine does not return */
  __stack_chk_fail();
code_r0x001008ee:
  pp_Var6 = __gnu_cxx::
            __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
            ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                         *)&it);
  bVar1 = ((*pp_Var6)->vid).num;
  pp_Var6 = __gnu_cxx::
            __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
            ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                         *)&it);
  sprintf(name,"/dev/%s%d",*(char **)(&prefixes + (long)(int)((*pp_Var6)->vid).typ * 8),(uint)bVar1)
  ;
  pp_Var6 = __gnu_cxx::
            __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
            ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                         *)&it);
  iVar4 = ioctl((*pp_Var6)->fid,0x80685600,&vcap);
  if (iVar4 == 0) {
    pp_Var6 = __gnu_cxx::
              __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
              ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                           *)&it);
    param5 = cam_status[(*pp_Var6)->st];
    pp_Var6 = __gnu_cxx::
              __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
              ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                           *)&it);
    printf("  %d: %s: %s, %s, %s\n",i,*(char **)(&prefixes + (long)(int)((*pp_Var6)->vid).typ * 8),
           name,(char *)vcap.card,param5);
    cnt = cnt + 1;
LAB_00100a1a:
    __gnu_cxx::
    __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
    ::operator++((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                  *)&it,0);
    i = i + 1;
  }
  goto LAB_0010087b;
}


void stop_all_threads(vector<_video_io_device*,std--allocator<_video_io_device*>> *vv)

{
  bool bVar1;
  reference pp_Var2;
  long in_FS_OFFSET;
  iterator it;
  iterator local_18;
  long local_10;
  
  local_10 = *(long *)(in_FS_OFFSET + 0x28);
  it = (_video_io_device **)0x0;
  it = (_video_io_device **)
       std::vector<_video_io_device*,std--allocator<_video_io_device*>>::begin(vv);
  while( true ) {
    local_18 = (_video_io_device **)
               std::vector<_video_io_device*,std--allocator<_video_io_device*>>::end(vv);
    bVar1 = __gnu_cxx::operator!=<_video_io_device**,std--vector<_video_io_device*>>
                      ((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                        *)&it,(__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                               *)&local_18);
    if (bVar1 == false) break;
    pp_Var2 = __gnu_cxx::
              __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
              ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                           *)&it);
    (*pp_Var2)->thon = 0;
    __gnu_cxx::
    __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
    ::operator++((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                  *)&it,0);
  }
  if (local_10 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return;
}


int stop_view(vector<_video_io_device*,std--allocator<_video_io_device*>> *vv)

{
  bool bVar1;
  reference pp_Var2;
  iterator __first;
  difference_type dVar3;
  long in_FS_OFFSET;
  int d;
  iterator it;
  iterator local_18;
  long local_10;
  
  local_10 = *(long *)(in_FS_OFFSET + 0x28);
  d = -1;
  it = (_video_io_device **)0x0;
  it = (_video_io_device **)
       std::vector<_video_io_device*,std--allocator<_video_io_device*>>::begin(vv);
  while( true ) {
    local_18 = (_video_io_device **)
               std::vector<_video_io_device*,std--allocator<_video_io_device*>>::end(vv);
    bVar1 = __gnu_cxx::operator!=<_video_io_device**,std--vector<_video_io_device*>>
                      ((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                        *)&it,(__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                               *)&local_18);
    if (bVar1 == false) break;
    pp_Var2 = __gnu_cxx::
              __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
              ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                           *)&it);
    if ((*pp_Var2)->view != 0) {
      __first = std::vector<_video_io_device*,std--allocator<_video_io_device*>>::begin(vv);
      dVar3 = std::
              distance<__gnu_cxx--__normal_iterator<_video_io_device**,std--vector<_video_io_device*>>>
                        ((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                          )__first,
                         (__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                          )it);
      d = (int)dVar3;
    }
    pp_Var2 = __gnu_cxx::
              __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
              ::operator*((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                           *)&it);
    (*pp_Var2)->view = 0;
    __gnu_cxx::
    __normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
    ::operator++((__normal_iterator<_video_io_device**,std--vector<_video_io_device*,std--allocator<_video_io_device*>>>
                  *)&it,0);
  }
  if (local_10 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return d;
}



int update_cam_format(viod *p_vio)

{
  long lVar1;
  int iVar2;
  long in_FS_OFFSET;
  v4l2_format fm;
  
  lVar1 = *(long *)(in_FS_OFFSET + 0x28);
  memset(&fm,0,0xd0);
  fm.type = 1;
  iVar2 = ioctl(p_vio->fid,0xc0d05604,&fm);
  if (iVar2 == -1) {
    iVar2 = -1;
  }
  else {
    p_vio->h = fm.fmt._4_4_;
    p_vio->w = fm.fmt._0_4_;
    p_vio->pxfmt = fm.fmt._8_4_;
    iVar2 = 0;
  }
  if (lVar1 != *(long *)(in_FS_OFFSET + 0x28)) {
                    /* WARNING: Subroutine does not return */
    __stack_chk_fail();
  }
  return iVar2;
}













