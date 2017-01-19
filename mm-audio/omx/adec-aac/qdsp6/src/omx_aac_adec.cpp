/*============================================================================
  @file omx_aac_adec.c
  This module contains the implementation of the OpenMAX Audio AAC component.

  Copyright (c) 2011-2012,2014 Qualcomm Technologies, Inc.
  All Rights Reserved. Qualcomm Technologies Proprietary and Confidential.
*//*========================================================================*/

//////////////////////////////////////////////////////////////////////////////
//                             Include Files
////////////////////////////////////////////////////////////////////////////
//#define LOG_NDDEBUG 0
//#define LOG_NDEBUG  0

#include<omx_aac_adec.h>

#if DUMPS_ENABLE
int bfd=-1,pfd=-1;
#endif

using namespace std;

void *get_omx_component_factory_fn(void)
{
    return (new COmxAacDec);
}

// omx_cmd_queue destructor
COmxAacDec::omx_cmd_queue::~omx_cmd_queue()
{
    // Nothing to do
}

// omx cmd queue constructor
COmxAacDec::omx_cmd_queue::omx_cmd_queue(): m_read(0),m_write(0),m_size(0)
{
    memset(m_q, 0, sizeof(omx_event)*OMX_CORE_CONTROL_CMDQ_SIZE);
}

// omx cmd queue insert
bool COmxAacDec::omx_cmd_queue::insert_entry(unsigned p1,
        unsigned p2, unsigned id)
{
    bool ret = true;
    if(m_size < OMX_CORE_CONTROL_CMDQ_SIZE) {
        m_q[m_write].id       = id;
        m_q[m_write].param1   = p1;
        m_q[m_write].param2   = p2;
        m_write++;
        m_size ++;
        if(m_write >= OMX_CORE_CONTROL_CMDQ_SIZE) {
            m_write = 0;
        }
    } else {
        ret = false;
        DEBUG_PRINT_ERROR("ERROR!!! Command Queue Full");
    }
    return ret;
}

// omx cmd queue delete
bool COmxAacDec::omx_cmd_queue::pop_entry(unsigned *p1,
        unsigned *p2, unsigned *id)
{
    bool ret = true;
    if (m_size > 0)
    {
        *id = m_q[m_read].id;
        *p1 = m_q[m_read].param1;
        *p2 = m_q[m_read].param2;
        // Move the read pointer ahead
        ++m_read;
        --m_size;
        if(m_read >= OMX_CORE_CONTROL_CMDQ_SIZE)
        {
            m_read = 0;

        }
    }
    else
    {
        ret = false;
        DEBUG_PRINT_ERROR("ERROR Delete!!! Command Queue Empty");
    }
    return ret;
}

bool COmxAacDec::omx_cmd_queue::get_msg_id(unsigned *id)
{
    if(m_size > 0)
    {
        *id = m_q[m_read].id;
        DEBUG_PRINT("get_msg_id=%d\n",*id);
    }
    else
    {
        return false;
    }
    return true;
}

void COmxAacDec::wait_for_event()
{
    pthread_mutex_lock(&m_event_lock);
    while(m_is_event_done == 0)
    {
        pthread_cond_wait(&cond, &m_event_lock);
    }
    m_is_event_done = 0;
    pthread_mutex_unlock(&m_event_lock);
}

void COmxAacDec::event_complete()
{
    pthread_mutex_lock(&m_event_lock);
    if(m_is_event_done == 0)
    {
        m_is_event_done = 1;
        pthread_cond_signal(&cond);
    }
    pthread_mutex_unlock(&m_event_lock);
}

void COmxAacDec::in_th_sleep()
{
    DEBUG_DETAIL("%s",__FUNCTION__);
    pthread_mutex_lock(&m_in_th_lock_1);
    is_in_th_sleep = true;
    pthread_mutex_unlock(&m_in_th_lock_1);
    in_sleep();
    DEBUG_PRINT("GOING TO SLEEEP\n");
}

void COmxAacDec::in_sleep()
{
    pthread_mutex_lock(&m_in_th_lock);
    while (m_is_in_th_sleep == 0)
    {
        pthread_cond_wait(&in_cond, &m_in_th_lock);
    }
    m_is_in_th_sleep = 0;
    pthread_mutex_unlock(&m_in_th_lock);
}

void COmxAacDec::in_th_wakeup()
{
    DEBUG_DETAIL("%s",__FUNCTION__);
    pthread_mutex_lock(&m_in_th_lock);
    if (m_is_in_th_sleep == 0)
    {
        m_is_in_th_sleep = 1;
        pthread_cond_signal(&in_cond);
    }
    pthread_mutex_unlock(&m_in_th_lock);
    DEBUG_PRINT("in sleep***********\n");
}

void COmxAacDec::out_th_sleep()
{
    DEBUG_DETAIL("%s",__FUNCTION__);
    pthread_mutex_lock(&m_out_th_lock_1);
    is_out_th_sleep = true;
    pthread_mutex_unlock(&m_out_th_lock_1);
    out_sleep();
}

void COmxAacDec::out_sleep()
{
    pthread_mutex_lock(&m_out_th_lock);
    while (m_is_out_th_sleep == 0)
    {
        pthread_cond_wait(&out_cond, &m_out_th_lock);
    }
    m_is_out_th_sleep = 0;
    pthread_mutex_unlock(&m_out_th_lock);
}

void COmxAacDec::out_th_wakeup()
{
    DEBUG_DETAIL("%s",__FUNCTION__);
    pthread_mutex_lock(&m_out_th_lock);
    if (m_is_out_th_sleep == 0)
    {
        m_is_out_th_sleep = 1;
        pthread_cond_signal(&out_cond);
    }
    pthread_mutex_unlock(&m_out_th_lock);
    DEBUG_PRINT("Out sleep***********\n");
}

void COmxAacDec::wait_for_flush_event()
{
    pthread_mutex_lock(&m_flush_cmpl_lock);
    m_flush_cmpl_event = 1;
    pthread_mutex_unlock(&m_flush_cmpl_lock);
    sem_wait(&sem_flush_cmpl_state);
}

void COmxAacDec::event_flush_complete()
{
    pthread_mutex_lock(&m_flush_cmpl_lock);
    if(m_flush_cmpl_event == 1)
    {
        sem_post(&sem_flush_cmpl_state);
        m_flush_cmpl_event = 0;
    }
    if(adif)
        nTimestamp = 0;
    pthread_mutex_unlock(&m_flush_cmpl_lock);
}

/** ======================================================================
@brief static member function for handling all events from kernel

Kernel events are processed through this routine.
@param client_data pointer to decoder component
@param id command identifier
@return none
========================================================================*/

void COmxAacDec::process_event_cb(void *client_data, unsigned char id)
{
    /* To remove warning for unused variable to keep prototype same */
    (void)id;
    DEBUG_PRINT("PE:Waiting for event's...");
    COmxAacDec  *pThis = (COmxAacDec *) client_data;
    pThis->process_events(pThis);
}

void COmxAacDec::process_events(COmxAacDec *client_data)
{
    int rc = 0;
    META_OUT *pmeta_out = NULL;
    DEC_META_OUT *dec_meta_out = NULL;
    OMX_BUFFERHEADERTYPE *bufHdr = NULL;
    struct msm_audio_event drv_event;
    memset(&drv_event, 0, sizeof(msm_audio_event));

    /* To remove warning for unused variable to keep prototype same */
    (void)client_data;
    // This loop waits indefintely till an EVENT is recieved.
    while(1)
    {
        DEBUG_PRINT("%s[%p]\n",__FUNCTION__,this);
        rc = ioctl(m_drv_fd,AUDIO_GET_EVENT,&drv_event);
        if(rc < 0)
        {
            DEBUG_PRINT_ERROR("EventThread exiting rc[%d] \
                    errno[%d]", rc, errno);
            return;
        }
        DEBUG_PRINT("AUDIO_GET_EVENT rc[0x%x]", rc);
        DEBUG_PRINT("PE:suspensionpolicy[%d] event[%d]",
            suspensionPolicy, drv_event.event_type);
        switch(drv_event.event_type)
        {
            case AUDIO_EVENT_SUSPEND:
            {
                DEBUG_PRINT("%s[%p]Suspend not processed",
                        __FUNCTION__, this);
                break;
            }
            case AUDIO_EVENT_RESUME:
            {
                DEBUG_PRINT("Resume not processed\n");
                break;
            }
            case AUDIO_EVENT_WRITE_DONE:
            {
                DEBUG_PRINT("ASYNC WRITE DONE from driver 0x%8x\n",
                (unsigned int)drv_event.event_payload.aio_buf.private_data);
                if(pcm_feedback)
                {
                    drv_event.event_payload.aio_buf.data_len -=
                        sizeof(META_IN);
                }
                bufHdr = (OMX_BUFFERHEADERTYPE*)
                    drv_event.event_payload.
                    aio_buf.private_data;
                if(bufHdr)
                {
                    bufHdr->nFilledLen =
                        drv_event.event_payload.
                        aio_buf.data_len;
                    DEBUG_PRINT("W-D event, bufHdr[%p] buf\
                        [%p] len[0x%x]", bufHdr,
                        bufHdr->pBuffer,
                        (unsigned int)bufHdr->nFilledLen);
                    post_input((unsigned) &m_cmp,
                        (unsigned int)bufHdr,
                    OMX_COMPONENT_GENERATE_BUFFER_DONE);
                    dec_num_drv_in_buf();
                }
                else
                {
                    DEBUG_PRINT_ERROR("%s[%p]W-D event, \
                    invalid bufHdr[%p]", __FUNCTION__,
                    this, bufHdr);
                }
                break;
            }
            case AUDIO_EVENT_READ_DONE:
            {
                unsigned int total_frame_size = 0, idx;
                bufHdr = (OMX_BUFFERHEADERTYPE*)drv_event.
                    event_payload.aio_buf.private_data;
                DEBUG_PRINT("ASYNC READ DONE from driver 0x%8x\n",
                (unsigned int)drv_event.event_payload.aio_buf.private_data);

                if(bufHdr)
                {
                   if(drv_event.event_payload.aio_buf.data_len > sizeof(META_OUT)) {
                        drv_event.event_payload.aio_buf.data_len -= sizeof(META_OUT);

                        pmeta_out = (META_OUT*) (bufHdr->pBuffer - sizeof(META_OUT));
                        if(!pmeta_out)
                        {
                            DEBUG_PRINT_ERROR("%s[%p]R-D, \
                                pmeta_out(NULL)",
                                __FUNCTION__, this);
                            return;
                        }

                        // Buffer from Driver will have
                        // (sizeof(DEC_META_OUT) * Nr of frame) bytes => meta_out->offset_to_frame
                        // Frame Size * Nr of frame =>
                        dec_meta_out = (DEC_META_OUT *)bufHdr->pBuffer;
                        // First frame offset is actual offset
                        bufHdr->nOffset =
                            dec_meta_out->offset_to_frame;
                        // First frame nflags actual nflags
                        bufHdr->nFlags |= dec_meta_out->nflags;
                        if(pmeta_out->num_of_frames != 0xFFFFFFFF) {
                            for(idx=0; idx < pmeta_out->num_of_frames; idx++) {
                                total_frame_size +=  dec_meta_out->frame_size;
                                dec_meta_out++;
                            }
                            DEBUG_PRINT("total pcm size %d\n", total_frame_size);
                        }
                        bufHdr->nFilledLen = total_frame_size;
                                        //If PCM samples reported zero by DSP during flush,
                                        //reset the offset value to zero, as the field not garanteed proper
                                        if(!bufHdr->nFilledLen)
						bufHdr->nOffset = 0;
                        #if DUMPS_ENABLE
                        DEBUG_PRINT_ERROR("ASYNC READ log to local buff %d", pfd);
                        write(pfd, (bufHdr->pBuffer + bufHdr->nOffset), bufHdr->nFilledLen);
                        #endif
                   } else {
                       DEBUG_PRINT_ERROR("Received less buffer with no pcm samples or at flush \
                                         data_len - %d\n", drv_event.event_payload.aio_buf.data_len);
                       bufHdr->nFilledLen = 0;
                       bufHdr->nOffset = 0;
                   }
                   DEBUG_PRINT("%s[%p]R-D, bufHdr[%p] buffer[%p] len[0x%x] offset 0x%x,, flags = 0x%x\n",
                               __FUNCTION__, this,
                               bufHdr, bufHdr->pBuffer,
                               (unsigned int)bufHdr->nFilledLen,
                               (unsigned int)bufHdr->nOffset,
                               (unsigned int)bufHdr->nFlags);

                   post_output((unsigned) &m_cmp,
                               (unsigned int)bufHdr,
                               OMX_COMPONENT_GENERATE_FRAME_DONE);
                   pthread_mutex_lock(&out_buf_count_lock);
                   drv_out_buf_cnt--;
                   pthread_mutex_unlock(&out_buf_count_lock);
                }
                else
                {
                    DEBUG_PRINT_ERROR("%s[%p]R-D, BufHdr \
                    Null", __FUNCTION__, this);
                }
                break;
            }
            case AUDIO_EVENT_STREAM_INFO:
            {
               if(!pcm_feedback)
                {
                    DEBUG_PRINT("PE:Rxed StreamInfo for tunnel mode\n");
                    break;
                }
                DEBUG_PRINT("PE: Recieved AUDIO_EVENT_STREAM_INFO");
                //ioctl(m_drv_fd, AUDIO_GET_STREAM_INFO,&stream_info);

                struct msm_audio_bitstream_info *pStrm =
                    (struct msm_audio_bitstream_info*)malloc(sizeof(struct msm_audio_bitstream_info));
		if (pStrm == NULL)
		{
			DEBUG_PRINT_ERROR("%s: Malloc failed\n", __FUNCTION__);
			break;
		}
                memcpy(pStrm,&drv_event.event_payload.stream_info,sizeof(struct msm_audio_bitstream_info));

                // command thread needs to free the memory allocated here.
                post_command(0,(unsigned int)pStrm,OMX_COMPONENT_STREAM_INFO);


                break;
            }

            default:
                DEBUG_PRINT("%s[%p]Invalid Event rxed[%d]",
                __FUNCTION__, this, drv_event.event_type);
                break;
        }
    }
    return;
}

/* ======================================================================
FUNCTION
COmxAacDec::COmxAacDec

DESCRIPTION
Constructor

PARAMETERS
None

RETURN VALUE
None.
========================================================================== */
COmxAacDec::COmxAacDec(): m_app_data(NULL),
                          m_eos_bm(0),
			  m_flush_cnt(0),
                          m_first_aac_header(0),
                          m_bytes_to_skip(0),
                          m_aac_hdr_bit_index(0),
                          m_is_alloc_buf(0),
                          drv_inp_buf_cnt(0),
                          drv_out_buf_cnt(0),
			  nNumInputBuf (0),
			  nNumOutputBuf(0),
                          m_drv_fd(-1),
                          ionfd(-1),
                          m_to_idle(false),
			  m_pause_to_exe(false),
                          is_in_th_sleep(false),
                          is_out_th_sleep(false),
			  bFlushinprogress(false),
			  bOutputPortReEnabled(false),
                          m_in_use_buf_case(false),
                          m_out_use_buf_case(false),
			  m_session_id(0),
                          m_inp_act_buf_count (OMX_CORE_NUM_INPUT_BUFFERS),
                          m_out_act_buf_count (OMX_CORE_NUM_OUTPUT_BUFFERS),
                          m_inp_current_buf_count(0),
                          m_out_current_buf_count(0),
                          m_comp_deinit(0),
                          m_flags(0),
			  m_fbd_cnt(0),
                          nTimestamp(0),
                          adif(false),
			  pcm_feedback(0),
                          output_buffer_size (OMX_AAC_OUTPUT_BUFFER_SIZE),
                          input_buffer_size (OMX_CORE_INPUT_BUFFER_SIZE),
                          m_ftb_cnt(0),
                          m_is_event_done(0),
			  m_is_in_th_sleep(0),
			  m_is_out_th_sleep(0),
                          m_flush_cmpl_event(0),
                          m_ipc_to_in_th(NULL),
                          m_ipc_to_out_th(NULL),
                          m_ipc_to_cmd_th(NULL),
                          m_ipc_to_event_th(NULL),
                          m_inp_bEnabled(OMX_TRUE),
                          m_inp_bPopulated(OMX_FALSE),
                          m_out_bEnabled(OMX_TRUE),
                          m_out_bPopulated(OMX_FALSE),
                          m_state(OMX_StateInvalid),
                          m_drv_buf_cnt(0),
                          m_volume(25),
                          pBitrate(0),
                          pChannels(0),
                          pSamplerate(0)
{
    int cond_ret = 0;
    memset(&m_cmp,       0,     sizeof(m_cmp));
    memset(&m_cb,        0,      sizeof(m_cb));
    memset(&m_adec_config_dualmono, 0, sizeof(m_adec_config_dualmono));
    memset(&m_adec_param,0, sizeof(m_adec_param));
    memset(&m_pcm_param,0, sizeof(m_pcm_param));
    memset(&m_priority_mgm, 0, sizeof(m_priority_mgm));
    memset(&m_buffer_supplier, 0, sizeof(m_buffer_supplier));
    memset(&component_Role, 0, sizeof(component_Role));

    memset(&m_cmd_q, 0, sizeof(m_cmd_q));
    memset(&m_din_q, 0, sizeof(m_din_q));
    memset(&m_cin_q, 0, sizeof(m_cin_q));
    memset(&m_dout_q, 0, sizeof(m_dout_q));
    memset(&m_cout_q, 0, sizeof(m_cout_q));
    memset(&m_fbd_q, 0, sizeof(m_fbd_q));
    memset(&m_ebd_q, 0, sizeof(m_ebd_q));

    suspensionPolicy= OMX_SuspensionDisabled;

    pthread_mutexattr_init(&m_inputlock_attr);
    pthread_mutex_init(&m_inputlock, &m_inputlock_attr);

    pthread_mutexattr_init(&m_state_lock_attr);
    pthread_mutex_init(&m_state_lock, &m_state_lock_attr);

    pthread_mutexattr_init(&m_commandlock_attr);
    pthread_mutex_init(&m_commandlock, &m_commandlock_attr);

    pthread_mutexattr_init(&m_outputlock_attr);
    pthread_mutex_init(&m_outputlock, &m_outputlock_attr);

    pthread_mutexattr_init(&m_event_attr);
    pthread_mutex_init(&m_event_lock, &m_event_attr);

    pthread_mutexattr_init(&m_flush_attr);
    pthread_mutex_init(&m_flush_lock, &m_flush_attr);

    pthread_mutexattr_init(&m_in_th_attr);
    pthread_mutex_init(&m_in_th_lock, &m_in_th_attr);

    pthread_mutexattr_init(&m_out_th_attr);
    pthread_mutex_init(&m_out_th_lock, &m_out_th_attr);

    pthread_mutexattr_init(&m_in_th_attr_1);
    pthread_mutex_init(&m_in_th_lock_1, &m_in_th_attr_1);

    pthread_mutexattr_init(&m_out_th_attr_1);
    pthread_mutex_init(&m_out_th_lock_1, &m_out_th_attr_1);

    pthread_mutexattr_init(&out_buf_count_lock_attr);
    pthread_mutex_init(&out_buf_count_lock, &out_buf_count_lock_attr);

    pthread_mutexattr_init(&in_buf_count_lock_attr);
    pthread_mutex_init(&in_buf_count_lock, &in_buf_count_lock_attr);

    pthread_mutexattr_init(&m_flush_cmpl_attr);
    pthread_mutex_init(&m_flush_cmpl_lock, &m_flush_cmpl_attr);

    if ((cond_ret = pthread_cond_init (&cond, NULL)) != 0)
    {
        DEBUG_PRINT_ERROR("pthread_cond_init returns non zero for cond\n");
        if (cond_ret == EAGAIN)
        DEBUG_PRINT_ERROR("The system lacked necessary resources(other than mem)\n");
        else if (cond_ret == ENOMEM)
        DEBUG_PRINT_ERROR("Insufficient memory to initialise condition variable\n");
    }

    if ((cond_ret = pthread_cond_init (&in_cond, NULL)) != 0)
    {
        DEBUG_PRINT_ERROR("pthread_cond_init returns non zero for in_cond\n");
        if (cond_ret == EAGAIN)
        DEBUG_PRINT_ERROR("The system lacked necessary resources(other than mem)\n");
        else if (cond_ret == ENOMEM)
        DEBUG_PRINT_ERROR("Insufficient memory to initialise condition variable\n");
    }

    if ((cond_ret = pthread_cond_init (&out_cond, NULL)) != 0)
    {
        DEBUG_PRINT_ERROR("pthread_cond_init returns non zero for out_cond\n");
        if (cond_ret == EAGAIN)
        DEBUG_PRINT_ERROR("The system lacked necessary resources(other than mem)\n");
        else if (cond_ret == ENOMEM)
        DEBUG_PRINT_ERROR("Insufficient memory to initialise condition variable\n");
    }

    sem_init(&sem_States,0, 0);
    sem_init(&sem_flush_cmpl_state, 0, 0);
    m_comp_deinit = 0;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    return;
}


/* ======================================================================
FUNCTION
COmxAacDec::~COmxAacDec

DESCRIPTION
Destructor

PARAMETERS
None

RETURN VALUE
None.
========================================================================== */
COmxAacDec::~COmxAacDec()
{
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    /* If component deinit is not happend, do it now before destroying */
    if ( !m_comp_deinit )
    {
        deinit_decoder();
    }
    pthread_mutex_destroy(&m_inputlock);
    pthread_mutex_destroy(&m_outputlock);
    pthread_mutexattr_destroy(&m_event_attr);
    pthread_mutex_destroy(&m_state_lock);
    pthread_mutex_destroy(&m_commandlock);
    pthread_mutex_destroy(&m_in_th_lock);
    pthread_mutex_destroy(&m_out_th_lock);
    pthread_mutex_destroy(&m_in_th_lock_1);
    pthread_mutex_destroy(&m_out_th_lock_1);
    pthread_mutex_destroy(&out_buf_count_lock);
    pthread_mutex_destroy(&in_buf_count_lock);
    pthread_mutexattr_destroy(&out_buf_count_lock_attr);
    pthread_mutexattr_destroy(&in_buf_count_lock_attr);
    pthread_mutex_destroy(&m_flush_cmpl_lock);
    pthread_mutex_destroy(&m_flush_lock);
    pthread_mutexattr_destroy(&m_inputlock_attr);
    pthread_mutexattr_destroy(&m_state_lock_attr);
    pthread_mutexattr_destroy(&m_commandlock_attr);
    pthread_mutexattr_destroy(&m_outputlock_attr);
    pthread_mutexattr_destroy(&m_flush_attr);
    pthread_mutexattr_destroy(&m_in_th_attr);
    pthread_mutexattr_destroy(&m_out_th_attr);
    pthread_mutexattr_destroy(&m_in_th_attr_1);
    pthread_mutexattr_destroy(&m_out_th_attr_1);
    pthread_mutexattr_destroy(&m_flush_cmpl_attr);

    pthread_cond_destroy(&cond);
    pthread_cond_destroy(&in_cond);
    pthread_cond_destroy(&out_cond);
    sem_destroy (&sem_States);
    sem_destroy (&sem_flush_cmpl_state);
    DEBUG_PRINT_ERROR("OMX AAC component destroyed\n");
    return;
}

bool COmxAacDec::aio_write(msm_audio_aio_buf *audio_aio_buf)
{
    bool rc = true;
    if( 0 > ioctl(m_drv_fd, AUDIO_ASYNC_WRITE, audio_aio_buf))
    {
        DEBUG_PRINT_ERROR("ASYNC WRITE fail[%d]", errno);
        rc = false;
    }
#if DUMPS_ENABLE
    DEBUG_PRINT_ERROR("ASYNC WRITE log to local buff");
    write(bfd, ((char *)audio_aio_buf->buf_addr + sizeof(META_IN)), (audio_aio_buf->data_len - sizeof(META_IN)));
#endif
    return rc;

}
bool COmxAacDec::aio_read(msm_audio_aio_buf *audio_aio_buf)
{
    bool rc = true;
    if( 0 > ioctl(m_drv_fd,AUDIO_ASYNC_READ , audio_aio_buf))
    {
        DEBUG_PRINT_ERROR("ASYNC READ FAIL[%d]", errno);
        rc = false;
    }
    return rc;

}

bool COmxAacDec::prepare_for_ebd(OMX_BUFFERHEADERTYPE *bufHdr)
{
    bool rc = true;
    META_IN *pmeta_in = NULL;
    msm_audio_aio_buf audio_aio_buf;

    if (bufHdr->nFlags & OMX_BUFFERFLAG_EOS )
    {
        if (bufHdr->nFilledLen)
        {
            rc = false;
            /* Set the Filled Length to 0 & EOS flag for the Buffer Header */
            bufHdr->nFilledLen = 0;
            set_eos_bm(IP_PORT_BITMASK);

            /* Asynchronous write call to the AAC driver */
            audio_aio_buf.buf_len = bufHdr->nAllocLen;
            audio_aio_buf.private_data = bufHdr;

            if (pcm_feedback)
            {
                pmeta_in = (META_IN*) (bufHdr->pBuffer - sizeof(META_IN));
                if(pmeta_in)
                {
                    pmeta_in->offsetVal  = sizeof(META_IN);
                    pmeta_in->nTimeStamp.LowPart =
                        ((((OMX_BUFFERHEADERTYPE*)bufHdr)->nTimeStamp)& 0xFFFFFFFF);
                    pmeta_in->nTimeStamp.HighPart =
                        (((((OMX_BUFFERHEADERTYPE*)bufHdr)->nTimeStamp) >> 32)
                         & 0xFFFFFFFF);
                    pmeta_in->nFlags = bufHdr->nFlags;
                    audio_aio_buf.buf_addr = (OMX_U8*)pmeta_in;
                    audio_aio_buf.data_len = bufHdr->nFilledLen +
                        sizeof(META_IN);
                    audio_aio_buf.mfield_sz = sizeof(META_IN);
                }
                else
                {
                    DEBUG_PRINT_ERROR("Invalid pmeta_in(NULL)");
                    return rc;
                }
            }
            else
            {
                audio_aio_buf.data_len = bufHdr->nFilledLen;
                audio_aio_buf.buf_addr = bufHdr->pBuffer;
                if((get_eos_bm() == IP_PORT_BITMASK))
                {
                    DEBUG_PRINT("***%s[%p]call fsync", __FUNCTION__, this);
                    fsync(m_drv_fd);
                    post_input((unsigned) & m_cmp,(unsigned) bufHdr,
                               OMX_COMPONENT_GENERATE_EOS);
                }
                return true;
            }

            pthread_mutex_lock(&in_buf_count_lock);
            drv_inp_buf_cnt++;
            pthread_mutex_unlock(&in_buf_count_lock);

            rc = aio_write(&audio_aio_buf);
            if (rc == false)
            {
                pthread_mutex_lock(&in_buf_count_lock);
                drv_inp_buf_cnt--;
                pthread_mutex_unlock(&in_buf_count_lock);
                return rc;
            }
            DEBUG_PRINT("%s::drv_inp_buf_cnt[%d]FilledLen[%d]", __FUNCTION__,
                drv_inp_buf_cnt, audio_aio_buf.data_len);
        }
        else
        {
            DEBUG_PRINT("%s::Mode[0x%x]nFilledLen[0x%x]nFlags[0x%x]",\
               __FUNCTION__, pcm_feedback, (unsigned int)bufHdr->nFilledLen,
                           (unsigned int)bufHdr->nFlags);
           if((get_eos_bm() == IP_PORT_BITMASK) && !(get_num_il_in_buf()))
           {
               rc = false;
           }
           else
           {
               rc = true;
           }
        }
    }
    if(rc)
    {
        DEBUG_PRINT("%s[%p]nNumInputBuf[0x%x]drv_inp_buf_cnt[0x%x]",
        __FUNCTION__, this, nNumInputBuf, drv_inp_buf_cnt);
        if(!pcm_feedback)
        {
            if((get_eos_bm() == IP_PORT_BITMASK))
            {
                DEBUG_PRINT("%s[%p]call fsync", __FUNCTION__, this);
                fsync(m_drv_fd);
                post_input((unsigned) & m_cmp,(unsigned) bufHdr,
                            OMX_COMPONENT_GENERATE_EOS);
            }
        }
    }
    return rc;
}

bool COmxAacDec::audio_register_ion(struct mmap_info *ion_buf)
{
    bool rc = true;
    struct msm_audio_ion_info audio_ion_buf;
    /* Register the mapped ION buffer with the AAC driver */
    audio_ion_buf.fd = ion_buf->ion_fd_data.fd;
    audio_ion_buf.vaddr = ion_buf->pBuffer;
    if(0 > ioctl(m_drv_fd, AUDIO_REGISTER_ION, &audio_ion_buf))
    {
        DEBUG_PRINT_ERROR("\n Error in ioctl AUDIO_REGISTER_ION\n");
        free_ion_buffer((void**)&ion_buf);
        return false;
    }
    return rc;
}

bool COmxAacDec::audio_deregister_ion(struct mmap_info *ion_buf)
{
    bool rc = true;
    struct msm_audio_ion_info audio_ion_buf;
    if (!ion_buf)
    {
        DEBUG_PRINT_ERROR("%s[%p] ion null", __FUNCTION__, this);
        return false;
    }
    audio_ion_buf.fd = ion_buf->ion_fd_data.fd;
    audio_ion_buf.vaddr = ion_buf->pBuffer;
    if(0 > ioctl(m_drv_fd, AUDIO_DEREGISTER_ION, &audio_ion_buf))
    {
        DEBUG_PRINT_ERROR("%s[%p]W-D, Buf, DEREG-ION,fd[%d] \
        ion-buf[%p]", __FUNCTION__, this, ion_buf->ion_fd_data.fd,
        ion_buf->pBuffer);
        rc = false;
    }
    return rc;
}

/**
@brief memory /function for sending EmptyBufferDone event
back to IL client

@param bufHdr OMX buffer header to be passed back to IL client
@return none
*/
void COmxAacDec::buffer_done_cb(OMX_BUFFERHEADERTYPE *bufHdr, bool flg)
{
    OMX_BUFFERHEADERTYPE *buffer = bufHdr;

    bool rc = true;
    if(m_state != OMX_StateInvalid)
        rc = prepare_for_ebd(bufHdr);

    if(m_in_use_buf_case && (flg == false) )
    {
        buffer = m_loc_in_use_buf_hdrs.find(bufHdr);
        if(!buffer)
        {
            DEBUG_PRINT_ERROR("Error,[%s][%p]invalid use buf hdr[%p]",\
                                   __FUNCTION__, (void*)this,(void*)bufHdr);
            return;
        }
        buffer->nFlags = bufHdr->nFlags;
        buffer->nFilledLen = bufHdr->nFilledLen;
    }

    if(m_cb.EmptyBufferDone && rc)
    {
        buffer->nFilledLen = 0 ;
        dec_num_il_in_buf();
        PrintFrameHdr(OMX_COMPONENT_GENERATE_BUFFER_DONE, buffer);
        m_cb.EmptyBufferDone(&m_cmp, m_app_data, buffer);
        DEBUG_PRINT("BUFFER DONE CB\n");
    }
    else
        DEBUG_PRINT("EBD: not happened, rc[%d]", rc);
    return;
}

void COmxAacDec::flush_ack()
{
    int lcnt = 0;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    pthread_mutex_lock(&m_flush_lock);
    --m_flush_cnt;
    lcnt = m_flush_cnt;
    DEBUG_PRINT("%s[%p]cnt=%d", __FUNCTION__, this, m_flush_cnt);
    pthread_mutex_unlock(&m_flush_lock);
    if( lcnt == 0 )
    {
        event_complete();
    event_flush_complete();
    }
}

void COmxAacDec::frame_done_cb(OMX_BUFFERHEADERTYPE *bufHdr, bool flg)
{
    META_OUT *pmeta_out = NULL;
    DEC_META_OUT *dec_meta_out = NULL;
    OMX_BUFFERHEADERTYPE *buffer = bufHdr;

    DEBUG_PRINT("%s[%p]", __FUNCTION__,this);
    if(flg == false) 
    {
    pmeta_out = (META_OUT*) (buffer->pBuffer - sizeof(META_OUT));
    if(!pmeta_out)
    {
        DEBUG_PRINT_ERROR("%s[%p]pmetaout(NULL)", __FUNCTION__, this);
        return;
    }
    dec_meta_out = (DEC_META_OUT *)buffer->pBuffer;
    DEBUG_PRINT("META_OUT TIMESTAMP msw %x lsw %x\n", dec_meta_out->msw_ts,
            dec_meta_out->lsw_ts);
    if(buffer->nFilledLen > buffer->nAllocLen)
    {
        DEBUG_PRINT_ERROR("%s[%p]Invalid FilledLen[0x%x] AllocLen[0x%x]",
        __FUNCTION__, this, (unsigned)(buffer->nFilledLen),
        (unsigned)(buffer->nAllocLen));
        buffer->nFilledLen=0;
        buffer->nOffset = 0;
    }
    //If PCM filled length is zero, which means no time stamp update expected
    //reset timestamp to older value. DSP will not send proper timestamp during flush
    if((adif) && buffer->nFilledLen)
    {
         OMX_U64 nOutputSamples = buffer->nFilledLen;
         // nOutSamples is output length in bytes
         // Time = (no. of outsamples in bytes)/(sampling rate* channel count * 2)
         //                - Denominator is multiplied by 2 to convert bytes to words.
         //                - IL Client expects the timestamp in microseconds.So, we multiply
         //                  number of samples with 1000000 to convert secs to micro secs.
         OMX_U64 nTime = (nOutputSamples * 1000000ll) /(m_adec_param.nSampleRate *m_adec_param.nChannels *2);
         buffer->nTimeStamp = nTime + nTimestamp;
         nTimestamp = buffer->nTimeStamp;
    }
    else if(buffer->nFilledLen) {
          buffer->nTimeStamp = ((((OMX_U64)dec_meta_out->msw_ts) << 32) + dec_meta_out->lsw_ts);
          nTimestamp = buffer->nTimeStamp;
    } else
          buffer->nTimeStamp = nTimestamp;
    // reset the nFlags, 0x80000000 indcates valid timestamp
    bufHdr->nFlags &= ~0x80000000;
    DEBUG_PRINT("FBD Timestamp: %ld, Flags[0x%x]", (long int)nTimestamp, (unsigned int)bufHdr->nFlags);
    }
    if(m_out_use_buf_case && (flg  == false))
    {
        buffer = m_loc_out_use_buf_hdrs.find(bufHdr);
        if(!buffer)
        {
            DEBUG_PRINT("%s[%p]UseBufhdr[%p] is NULL",__FUNCTION__, this, (void*)bufHdr);
            return;
        }
        buffer->nFilledLen = bufHdr->nFilledLen;
        buffer->nFlags = bufHdr->nFlags;
        buffer->nTimeStamp = nTimestamp;
        buffer->nOffset = bufHdr->nOffset;
        if(bufHdr->nFilledLen)
             memcpy(buffer->pBuffer+buffer->nOffset, bufHdr->pBuffer+bufHdr->nOffset, bufHdr->nFilledLen);
	// Reset the copy-buffer headers nflag status
        bufHdr->nFlags = 0x00;

    }

    if((get_eos_bm()& IP_OP_PORT_BITMASK) != IP_OP_PORT_BITMASK)
    {
        if(buffer->nFlags & OMX_BUFFERFLAG_EOS)
        {
            DEBUG_PRINT("%s[%p] EOS reached", __FUNCTION__, this);
        set_eos_bm(get_eos_bm() | OP_PORT_BITMASK);
            post_output(0, (unsigned)buffer, OMX_COMPONENT_GENERATE_EOS);
        }
    }

    if(m_cb.FillBufferDone)
    {
        PrintFrameHdr(OMX_COMPONENT_GENERATE_FRAME_DONE, buffer);
        pthread_mutex_lock(&out_buf_count_lock);
        m_fbd_cnt++;
        nNumOutputBuf--;
        DEBUG_PRINT("%s[%p] Cnt[%d] NumOutBuf[%d]", __FUNCTION__, this,
            m_fbd_cnt, nNumOutputBuf);
        pthread_mutex_unlock(&out_buf_count_lock);
        m_cb.FillBufferDone(&m_cmp, m_app_data, buffer);
    }
    return;
}

/** ======================================================================
@brief static member function for handling all commands from IL client

IL client commands are processed and callbacks are generated
through this routine. Audio Command Server provides the thread context
for this routine.

@param client_data pointer to decoder component
@param id command identifier
@return none
*/

void COmxAacDec::process_out_port_msg(void *client_data, unsigned char id)
{
    unsigned     p1;
    unsigned     p2;
    unsigned     ident;
    unsigned     qsize  = 0;
    unsigned     tot_qsize = 0;
    COmxAacDec *pThis = (COmxAacDec *) client_data;
    OMX_STATETYPE state;

    DEBUG_PRINT("%s %p\n",__FUNCTION__,pThis);

    loopback_out:
    state = pThis->get_state();

    if(state == OMX_StateLoaded)
    {
        DEBUG_PRINT("OUT: In Loaded state\n");
        return;
    }

    pthread_mutex_lock(&pThis->m_outputlock);

    qsize = pThis->m_cout_q.m_size;
    tot_qsize = qsize;
    tot_qsize += pThis->m_fbd_q.m_size;
    tot_qsize += pThis->m_dout_q.m_size;

    DEBUG_DETAIL("OUT-->state[%d] coutq[%d]doutq[%d]", state,\
    pThis->m_cout_q.m_size,
    pThis->m_dout_q.m_size);

    if(tot_qsize ==0) {
        pthread_mutex_unlock(&pThis->m_outputlock);
        DEBUG_DETAIL("OUT-->BREAK FROM LOOP...%d\n",tot_qsize);
        return;
    }

    if((state != OMX_StateExecuting) && (!pThis->m_cout_q.m_size))
    {
        pthread_mutex_unlock(&pThis->m_outputlock);
        if(state == OMX_StatePause) {
            usleep(10000);
            goto loopback_out;
        }
        DEBUG_DETAIL("OUT: Sleeping out thread state[%d] totq[%d] coutq %d\n",\
              state, tot_qsize, pThis->m_cout_q.m_size);
        pThis->out_th_sleep();
        goto loopback_out;
    }
    if((!pThis->bOutputPortReEnabled) && (!pThis->m_cout_q.m_size))
    {
        DEBUG_DETAIL("No flush/port reconfig qsize=%d tot_qsize=%d",\
        qsize,tot_qsize);
        pthread_mutex_unlock(&pThis->m_outputlock);
        if(pThis->m_cout_q.m_size || !(pThis->bFlushinprogress))
        {
            pThis->out_th_sleep();
        }
        goto loopback_out;
    }

    else if(state == OMX_StatePause)
    {
        DEBUG_PRINT ("\n OUT Thread in the pause state");
        if (!pThis->m_cout_q.m_size)
        {
            if (!pThis->is_pause_to_exe())
            {
                pthread_mutex_unlock(&pThis->m_outputlock);
                DEBUG_DETAIL("OUT: SLEEPING OUT THREAD\n");
                pThis->out_th_sleep();
                goto loopback_out;
            }
            else
            {
                DEBUG_PRINT("OUT--> In pause if() check, but now state \
                            changed\n");
            }
        }
    }

    qsize = pThis->m_cout_q.m_size;
    if (qsize)
    {
        pThis->m_cout_q.pop_entry(&p1,&p2,&ident);
    }
    else if((qsize = pThis->m_fbd_q.m_size) &&
            (pThis->bOutputPortReEnabled) && (state == OMX_StateExecuting))
    {
        pThis->m_fbd_q.pop_entry(&p1,&p2,&ident);
    }
    else if((qsize = pThis->m_dout_q.m_size) &&
            (pThis->bOutputPortReEnabled) && (state == OMX_StateExecuting))
    {
        pThis->m_dout_q.pop_entry(&p1,&p2,&ident);
    }
    else
    {
        qsize = 0;
        pthread_mutex_unlock(&pThis->m_outputlock);
        DEBUG_PRINT("MAKing qsize zero\n");
        goto loopback_out;
    }

    pthread_mutex_unlock(&pThis->m_outputlock);

    if(qsize > 0)
    {
        id = ident;
        DEBUG_DETAIL("OUT->state[%d]ident[%d]cout[%d]fbd[%d]dataq[%d]\n",\
        pThis->m_state,
        ident,
        pThis->m_cout_q.m_size,
        pThis->m_fbd_q.m_size,
        pThis->m_dout_q.m_size);

        if(id == OMX_COMPONENT_GENERATE_FRAME_DONE)
        {
            pThis->frame_done_cb((OMX_BUFFERHEADERTYPE *)p2);
        }
        else if(id == OMX_COMPONENT_GENERATE_FTB)
        {
            pThis->fill_this_buffer_proxy((OMX_HANDLETYPE)p1,
                                          (OMX_BUFFERHEADERTYPE *)p2);
        }
        else if(id == OMX_COMPONENT_GENERATE_EOS)
        {
            pThis->m_cb.EventHandler(&pThis->m_cmp,
            pThis->m_app_data,
            OMX_EventBufferFlag,
            1, 1, NULL );
        }
        else if(id == OMX_COMPONENT_PORTSETTINGS_CHANGED)
        {
            pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
            OMX_EventPortSettingsChanged, 1, 0, NULL );
        }
        else if(id == OMX_COMPONENT_SUSPEND)
        {
                DEBUG_PRINT("Suspend command on o/p port ignored\n");
        }
        else if(id == OMX_COMPONENT_RESUME)
        {
                DEBUG_PRINT("Resume command on o/p port ignored\n");
        }
        else if(id == OMX_COMPONENT_GENERATE_COMMAND)
        {
            if(p1 == OMX_CommandFlush)
            {
                DEBUG_DETAIL("Executing FLUSH command on Output port\n");
                pThis->execute_output_omx_flush();
            }
            else
            {
                DEBUG_DETAIL("Invalid command[%d]\n",p1);
            }
        }
        else
        {
            DEBUG_PRINT_ERROR("ERROR:OUT-->Invalid Id[%d]\n",id);
        }
    }
    else
    {
        DEBUG_DETAIL("ERROR: OUT--> Empty OUTPUTQ\n");
    }

    return;
}

void COmxAacDec::process_command_msg(void *client_data, unsigned char id)
{
    unsigned      p1;
    unsigned      p2;
    unsigned      ident;
    unsigned      qsize=0;
    COmxAacDec  *pThis = (COmxAacDec *) client_data;
    OMX_STATETYPE state = pThis->get_state();

    DEBUG_PRINT("%s %p\n",__FUNCTION__,pThis);
    pthread_mutex_lock(&pThis->m_commandlock);
    qsize = pThis->m_cmd_q.m_size;

    DEBUG_PRINT("CMD-->QSIZE=%d state=%d\n", \
            pThis->m_cmd_q.m_size,\
                    pThis->m_state);
    if(!qsize )
    {
        DEBUG_PRINT("CMD-->BREAKING FROM LOOP\n");
        pthread_mutex_unlock(&pThis->m_commandlock);
        return;
    }
    else
    {
        pThis->m_cmd_q.pop_entry(&p1,&p2,&ident);
    }
    pthread_mutex_unlock(&pThis->m_commandlock);

    id = ident;
    DEBUG_PRINT("CMD->state[%d]id[%d]cmdq[%d] \n",\
    pThis->m_state,ident, \
    pThis->m_cmd_q.m_size);

    if(id == OMX_COMPONENT_GENERATE_EVENT)
    {
        if (pThis->m_cb.EventHandler)
        {
            if (p1 == OMX_CommandStateSet)
            {
                pThis->set_state((OMX_STATETYPE)p2);
                state = pThis->get_state();
                DEBUG_PRINT("CMD:Process->state set to %d \n", state);

                if((state == OMX_StateExecuting) ||
                   (state == OMX_StateLoaded))
                {
                    pthread_mutex_lock(&pThis->m_in_th_lock_1);
                    if(pThis->is_in_th_sleep)
                    {
                        DEBUG_DETAIL("WAKE UP IN THREADS\n");
                        pThis->in_th_wakeup();
                        pThis->is_in_th_sleep = false;
                    }
                    pthread_mutex_unlock(&pThis->m_in_th_lock_1);

                    pthread_mutex_lock(&pThis->m_out_th_lock_1);
                    if(pThis->is_out_th_sleep)
                    {
                        DEBUG_DETAIL("WAKE UP OUT THREADS\n");
                        pThis->out_th_wakeup();
                        pThis->is_out_th_sleep = false;
                    }
                    pthread_mutex_unlock(&pThis->m_out_th_lock_1);
                }
                else if(state == OMX_StateIdle)
                {
                    pThis->m_to_idle=false;
                }
            }
            if (state == OMX_StateInvalid)
            {
                pThis->m_cb.EventHandler(&pThis->m_cmp,
                pThis->m_app_data,
                OMX_EventError,
                OMX_ErrorInvalidState,
                0, NULL );
            }
            else
            {
                pThis->m_cb.EventHandler(&pThis->m_cmp,
                pThis->m_app_data,
                OMX_EventCmdComplete,
                p1, p2, NULL );
            }
        }
        else
        {
            DEBUG_PRINT_ERROR("ERROR:CMD-->EventHandler NULL \n");
        }
    }
    else if(id == OMX_COMPONENT_GENERATE_COMMAND)
    {
        pThis->send_command_proxy(&pThis->m_cmp,
        (OMX_COMMANDTYPE)p1,
        (OMX_U32)p2,(OMX_PTR)NULL);
    }
    else if(id == OMX_COMPONENT_PORTSETTINGS_CHANGED)
    {
        DEBUG_DETAIL("CMD OMX_COMPONENT_PORTSETTINGS_CHANGED");
        pThis->m_cb.EventHandler(&pThis->m_cmp,
        pThis->m_app_data,
        OMX_EventPortSettingsChanged,
        1, 0, NULL );
    }
    else if(OMX_COMPONENT_SUSPEND == id)
    {
        DEBUG_PRINT("Suspend command ignored\n");
    }
    else if(id == OMX_COMPONENT_RESUME)
    {
        DEBUG_PRINT("Resume command ignored\n");
    }
    else if(id == OMX_COMPONENT_GENERATE_EOS)
    {
        pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
        OMX_EventBufferFlag, 1, 1, NULL );
    }
    else if (id == OMX_COMPONENT_STREAM_INFO)
    {
        struct msm_audio_bitstream_info *pStreamInfo = (struct msm_audio_bitstream_info *)p2;

        OMX_U32 sr = pStreamInfo->sample_rate;
        OMX_U32 ch = (pStreamInfo->chan_info & 0x0F);
        DEBUG_PRINT("CMD:StreamInfo-->cur SF[%d]New SF[%d] "
                    " cur ch=%d New ch=%d sbr-ps[%d]"
                    " cfg ch=%d sf=%d\n",
                    (unsigned int)pThis->m_adec_param.nSampleRate,(unsigned int)sr,
                    (unsigned int)pThis->m_adec_param.nChannels,(unsigned int)ch,
                    (unsigned int)pStreamInfo->bit_stream_info,
                    pStreamInfo->sample_rate,pStreamInfo->chan_info);
        pThis->m_adec_param.nFrameLength = OMX_ADEC_AAC_FRAME_LEN;
        // If SBR/PS present double the sampling rate
        if ( (sr == (pThis->m_adec_param.nSampleRate *2)))
        {
            // PCM driver can support sampling rates upto 48000 only
            if(sr<= 24000)
                pThis->m_adec_param.nFrameLength *= 2;
        } else if ((sr == (pThis->m_adec_param.nSampleRate / 2)))
        {
	    if(sr <= 24000)
                pThis->m_adec_param.nFrameLength /= 2;
        }
        free(pStreamInfo);
        if(ch > 2)
        {
            ch = 2;
            DEBUG_PRINT_ERROR("CMD-->Forced channel setting change...\n");
        }
        if(sr > 48000)
        {
            sr = 48000;
            DEBUG_PRINT_ERROR("CMD-->Forced frequency setting change...\n");
        }
        if(pThis->bFlushinprogress)
        {
            DEBUG_PRINT("FLUSH IN PROGRESS...\n");
            return;
        }
        // if rxed content is different from existing one, trigger port reconfiguration

        if((pThis->m_adec_param.nSampleRate != sr ) ||
            (pThis->m_adec_param.nChannels != ch ))
        {
            pThis->m_adec_param.nSampleRate = sr;
            pThis->m_adec_param.nChannels = ch;
            DEBUG_DETAIL("CMD-->Trigger OMX_EventPortSettingsChanged");
            pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
                                 OMX_EventPortSettingsChanged,
                                 1, 0, NULL );
         }
         else
         {
            // else just wake up output threads
            pThis->m_out_bEnabled = OMX_TRUE;
            pThis->bOutputPortReEnabled = 1;
            ioctl( pThis->m_drv_fd, AUDIO_OUTPORT_FLUSH, 0);

            DEBUG_PRINT("CMD--> No OMX_EventPortSettingsChanged");
            DEBUG_DETAIL("CMD:WAKING UP OUT THREADS\n");
            pThis->out_th_wakeup();
         }
    }
    else
    {
        DEBUG_PRINT_ERROR("CMD:Error--> incorrect event posted\n");
    }
    return;
}

void COmxAacDec::process_in_port_msg(void *client_data, unsigned char id)
{
    unsigned      p1; // Parameter - 1
    unsigned      p2; // Parameter - 2
    unsigned      ident;
    unsigned      qsize=0; // qsize
    unsigned      tot_qsize = 0;
    COmxAacDec  *pThis = (COmxAacDec *) client_data;
    OMX_STATETYPE state;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,pThis);
    if(!pThis)
    {
        DEBUG_PRINT_ERROR("IN:ERROR : Context is incorrect; bailing out\n");
        return;
    }

    loopback_in:
    state = pThis->get_state();
    if(state == OMX_StateLoaded)
    {
        DEBUG_PRINT(" IN: IN LOADED STATE RETURN\n");
        return;
    }
    // Protect the shared queue data structure
    pthread_mutex_lock(&pThis->m_inputlock);

    qsize = pThis->m_cin_q.m_size;
    tot_qsize = qsize;
    tot_qsize += pThis->m_ebd_q.m_size;
    tot_qsize += pThis->m_din_q.m_size;
    DEBUG_DETAIL("Input-->state[%d]cin[%d]ebd[%d]data[%d]",state,\
    pThis->m_cin_q.m_size,
    pThis->m_ebd_q.m_size,
    pThis->m_din_q.m_size);

    if(tot_qsize ==0) {
        DEBUG_DETAIL("IN-->BREAKING FROM IN LOOP");
        pthread_mutex_unlock(&pThis->m_inputlock);
        return;
    }
    if ( (state != OMX_StateExecuting) && !(pThis->m_cin_q.m_size))
    {
        pthread_mutex_unlock(&pThis->m_inputlock);

        DEBUG_DETAIL("SLEEPING IN THREAD\n");
        pThis->in_th_sleep();
        goto loopback_in;
    }
    else if ((state == OMX_StatePause))
    {
        if(( !pThis->m_cin_q.m_size) )
        {
            pthread_mutex_unlock(&pThis->m_inputlock);

            DEBUG_DETAIL("IN: Sleeping in thread state[%d] cinq[%d]", \
                state, pThis->m_cin_q.m_size);
            pThis->in_th_sleep();
            goto loopback_in;
        }
    }

    qsize = pThis->m_cin_q.m_size;
    if(qsize)
    {
        pThis->m_cin_q.pop_entry(&p1,&p2,&ident);
    }
    else if((qsize = pThis->m_ebd_q.m_size) &&
            (state == OMX_StateExecuting))
    {
        pThis->m_ebd_q.pop_entry(&p1,&p2,&ident);
    }
    else if((qsize = pThis->m_din_q.m_size) &&
            (state == OMX_StateExecuting))
    {
        pThis->m_din_q.pop_entry(&p1, &p2, &ident);
    }
    else
    {
        qsize = 0;
        if(state == OMX_StatePause)
    {
        pthread_mutex_unlock(&pThis->m_inputlock);
            DEBUG_DETAIL("IN: Sleeping in thread state[%d] cinq[%d]", \
                state, pThis->m_cin_q.m_size);
            pThis->in_th_sleep();
            goto loopback_in;
    }
    }
    pthread_mutex_unlock(&pThis->m_inputlock);
    if(qsize > 0)
    {
        id = ident;
        DEBUG_DETAIL("Input->state[%d]id[%d]cin[%d]ebdq[%d]dataq[%d]\n",\
        state, ident, pThis->m_cin_q.m_size,
        pThis->m_ebd_q.m_size, pThis->m_din_q.m_size);

        if(id == OMX_COMPONENT_GENERATE_BUFFER_DONE)
        {
            DEBUG_PRINT("GENERATE EBD\n");
            pThis->buffer_done_cb((OMX_BUFFERHEADERTYPE *)p2);
        }
        else if(id == OMX_COMPONENT_GENERATE_EOS)
        {
            pThis->m_cb.EventHandler(&pThis->m_cmp, pThis->m_app_data,
            OMX_EventBufferFlag, 0, 1, NULL );
        }
        else if(id == OMX_COMPONENT_GENERATE_ETB)
        {
            pThis->empty_this_buffer_proxy((OMX_HANDLETYPE)p1,
            (OMX_BUFFERHEADERTYPE *)p2);
        }
        else if(id == OMX_COMPONENT_GENERATE_COMMAND)
        {
            if(p1 == OMX_CommandFlush)
            {
                DEBUG_DETAIL(" Executing FLUSH command on Input port\n");
                pThis->execute_input_omx_flush();
            }
            else
            {
                DEBUG_DETAIL("Invalid command[%d]\n",p1);
            }
        }
        else if(id == OMX_COMPONENT_SUSPEND)
        {
        DEBUG_PRINT("Suspend command ignored on i/p port\n");
    }
        else
        {
            DEBUG_PRINT_ERROR("IN:Error-->Input thread invalid msg id[%d]", id);
        }
    }
    else
    {
        DEBUG_DETAIL("Error-->No messages in queue\n");
    }
    return ;
}

/**
@brief member function for performing component initialization

@param role C string mandating role of this component
@return Error status
*/
OMX_ERRORTYPE COmxAacDec::component_init(OMX_STRING role)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    m_state            = OMX_StateLoaded;
    char *dev_name = NULL;
    unsigned int mask = O_RDWR;
    m_is_in_th_sleep=0;
    m_is_out_th_sleep=0;

    /* DSP does not give information about the bitstream
    randomly assign the value right now. Query will result in
    incorrect param */
    memset(&m_adec_param, 0, sizeof(m_adec_param));
    m_adec_param.nSize = sizeof(m_adec_param);
    m_adec_param.nSampleRate = OMX_ADEC_DEFAULT_SF;
    m_adec_param.nChannels = OMX_ADEC_DEFAULT_CH_CFG;
    m_adec_param.nFrameLength = OMX_ADEC_AAC_FRAME_LEN;
    m_adec_param.eChannelMode = OMX_AUDIO_ChannelModeStereo;
    m_adec_param.eAACProfile = OMX_AUDIO_AACObjectLC;
    m_adec_param.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP2ADTS;

    memset(&m_adec_config_dualmono, 0, sizeof(m_adec_config_dualmono));
    m_adec_config_dualmono.nSize = sizeof(m_adec_config_dualmono);
    /* Disable sending by default*/
    m_adec_config_dualmono.eChannelConfig = OMX_AUDIO_DUAL_MONO_MODE_INVALID;

    m_volume = 25; /* Close to unity gain */
    pSamplerate = OMX_ADEC_DEFAULT_SF;
    pChannels = OMX_ADEC_DEFAULT_CH_CFG;

    m_pcm_param.nChannels = OMX_ADEC_DEFAULT_CH_CFG;
    m_pcm_param.eNumData = OMX_NumericalDataSigned;
    m_pcm_param.bInterleaved = OMX_TRUE;
    m_pcm_param.nBitPerSample = 16;
    m_pcm_param.nSamplingRate = OMX_ADEC_DEFAULT_SF;
    m_pcm_param.ePCMMode = OMX_AUDIO_PCMModeLinear;
    m_pcm_param.eChannelMapping[0] = OMX_AUDIO_ChannelLF;
    m_pcm_param.eChannelMapping[1] = OMX_AUDIO_ChannelRF;

    m_fbd_cnt = 0;
    nTimestamp = 0;
    adif = false;
    pcm_feedback = 1;    /* by default enable non-tunnel mode */
    bFlushinprogress = 0;
    nNumInputBuf = 0;
    nNumOutputBuf = 0;
    m_ipc_to_in_th = NULL;  // Command server instance
    m_ipc_to_out_th = NULL;  // Client server instance
    m_ipc_to_cmd_th = NULL;  // command instance
    m_ipc_to_event_th = NULL;
    m_first_aac_header = 0;
    m_flush_cmpl_event=0;
    suspensionPolicy= OMX_SuspensionDisabled;
    m_to_idle =false;
    bOutputPortReEnabled = 0;
    memset(&m_priority_mgm, 0, sizeof(m_priority_mgm));
    m_priority_mgm.nGroupID =0;
    m_priority_mgm.nGroupPriority=0;
    memset(&m_buffer_supplier, 0, sizeof(m_buffer_supplier));
    m_buffer_supplier.nPortIndex=OMX_BufferSupplyUnspecified;
    drv_inp_buf_cnt = 0;
    drv_out_buf_cnt = 0;
    DEBUG_PRINT_ERROR(" component init: role Test******* = %s\n",role);
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);

    /* Open the AAC device in asynchronous mode */
    if(!strcmp(role,"OMX.qcom.audio.decoder.aac"))
    {
        pcm_feedback = 1;
        dev_name = "/dev/msm_aac";
        mask = O_RDWR | O_NONBLOCK;
        DEBUG_PRINT("\ncomponent_init: Component %s LOADED \n", role);
    }
    else if(!strcmp(role,"OMX.qcom.audio.decoder.tunneled.aac"))
    {
        pcm_feedback = 0;
        dev_name = "/dev/msm_aac";
        mask = O_WRONLY | O_NONBLOCK;
        DEBUG_PRINT("\ncomponent_init: Component %s LOADED \n", role);
    }
    else if(!strcmp(role,"OMX.qcom.audio.decoder.multiaac"))
    {
        pcm_feedback = 1;
        dev_name = "/dev/msm_multi_aac";
        mask = O_RDWR | O_NONBLOCK;
        DEBUG_PRINT("\ncomponent_init: Component %s LOADED \n", role);
    }
    else if(!strcmp(role,"OMX.qcom.audio.decoder.tunneled.multiaac"))
    {
        pcm_feedback = 0;
        dev_name = "/dev/msm_multi_aac";
        mask = O_WRONLY | O_NONBLOCK;
        DEBUG_PRINT("\ncomponent_init: Component %s LOADED \n", role);
    }
    else
    {
        DEBUG_PRINT("\ncomponent_init: Component %s LOADED is invalid\n", role);
    }

    if(dev_name == NULL)
    {
      DEBUG_PRINT_ERROR("component_init:dev_name is NULL\n");
      return OMX_ErrorHardware;
    }
    m_drv_fd = open(dev_name, mask);

    if (m_drv_fd < 0)
    {
        DEBUG_PRINT_ERROR("component_init: device open fail \
            Loaded -->Invalid\n");
        return OMX_ErrorInsufficientResources;
    }

    if(ioctl(m_drv_fd, AUDIO_GET_SESSION_ID,&m_session_id) == -1)
    {
        DEBUG_PRINT("AUDIO_GET_SESSION_ID FAILED\n");
    }
    if(!m_ipc_to_in_th)
    {
        m_ipc_to_in_th = omx_aac_thread_create(process_in_port_msg, this,
        (char *) "INPUT_THREAD");
        if(!m_ipc_to_in_th)
        {
            DEBUG_PRINT_ERROR("ERROR!!!INPUT THREAD failed to get created\n");
            return OMX_ErrorHardware;
        }
    }
    if(!m_ipc_to_cmd_th)
    {
        m_ipc_to_cmd_th = omx_aac_thread_create(process_command_msg, this,
        (char *) "COMMAND_THREAD");
        if(!m_ipc_to_cmd_th)
        {
            DEBUG_PRINT_ERROR("ERROR!!! COMMAND THREAD failed to get \
                created\n");
            return OMX_ErrorHardware;
        }
    }
    if(pcm_feedback)
    {
        if(!m_ipc_to_out_th)
        {
            m_ipc_to_out_th = omx_aac_thread_create(process_out_port_msg, this,
            (char *) "OUTPUT_THREAD");
            if(!m_ipc_to_out_th)
            {
                DEBUG_PRINT_ERROR("ERROR!!! OUTPUT THREAD failed to get \
                created\n");
                return OMX_ErrorHardware;
            }
        }
    }
    DEBUG_PRINT_ERROR("%s: role[%s]\n", __FUNCTION__, role);

    ionfd = open("/dev/ion", O_RDONLY);
    if (ionfd < 0)
    {
        DEBUG_PRINT_ERROR("/dev/ion open failed \n");
        return OMX_ErrorInsufficientResources;
    }
#if DUMPS_ENABLE
    bfd = open("/sdcard/bitstreamdump.dat", O_RDWR|O_CREAT, 0666);
    if (bfd < 0) {
    perror("cannot open bitstreamdump file");
    }
    pfd = open("/sdcard/pcmdump.dat", O_RDWR|O_CREAT, 0666);
    if (pfd < 0) {
    perror("cannot open pcmdump file");
    }
#endif
    return eRet;
}

/**
@brief member function to retrieve version of component
@param hComp handle to this component instance
@param componentName name of component
@param componentVersion  pointer to memory space which stores the
    version number
@param specVersion pointer to memory sapce which stores version of
        openMax specification
@param componentUUID
@return Error status
*/
OMX_ERRORTYPE  COmxAacDec::get_component_version(
                                     OMX_IN OMX_HANDLETYPE    hComp,
                                     OMX_OUT OMX_STRING       componentName,
                                     OMX_OUT OMX_VERSIONTYPE* componentVersion,
                                     OMX_OUT OMX_VERSIONTYPE* specVersion,
                                     OMX_OUT OMX_UUIDTYPE*       componentUUID)
{
    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    (void)componentName;
    (void)componentVersion;
    (void)specVersion;
    (void)componentUUID;
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Get Comp Version in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    return OMX_ErrorNone;
}
/**
@brief member function handles command from IL client

This function simply queue up commands from IL client.
Commands will be processed in command server thread context later

@param hComp handle to component instance
@param cmd type of command
@param param1 parameters associated with the command type
@param cmdData
@return Error status
*/
OMX_ERRORTYPE  COmxAacDec::send_command(OMX_IN OMX_HANDLETYPE hComp,
                                        OMX_IN OMX_COMMANDTYPE  cmd,
                                        OMX_IN OMX_U32       param1,
                                        OMX_IN OMX_PTR      cmdData)
{
    int portIndex = (int)param1;
    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    (void)cmdData;

    DEBUG_PRINT("%s[%p]",__FUNCTION__,this);
    if(OMX_StateInvalid == m_state)
    {
        return OMX_ErrorInvalidState;
    }
    if ( (cmd == OMX_CommandFlush) && (portIndex > 1) )
    {
        return OMX_ErrorBadPortIndex;
    }

    post_command((unsigned)cmd, (unsigned)param1, \
            OMX_COMPONENT_GENERATE_COMMAND);

    DEBUG_PRINT("%s[%p]cmd[%d] semwait[%d]", __FUNCTION__, this,
                                cmd, (signed)param1);
    sem_wait (&sem_States);
    DEBUG_PRINT("%s[%p]cmd[%d] rxed sem post[%d]", __FUNCTION__,
                          this, cmd, (signed)param1);

    return OMX_ErrorNone;
}

/**
@brief member function performs actual processing of commands excluding
empty buffer call

@param hComp handle to component
@param cmd command type
@param param1 parameter associated with the command
@param cmdData

@return error status
*/
OMX_ERRORTYPE  COmxAacDec::send_command_proxy(OMX_IN OMX_HANDLETYPE hComp,
                                              OMX_IN OMX_COMMANDTYPE  cmd,
                                              OMX_IN OMX_U32       param1,
                                              OMX_IN OMX_PTR      cmdData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    //   Handle only IDLE and executing
    OMX_STATETYPE eState = (OMX_STATETYPE) param1;
    int bFlag = 1;

    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    (void)cmdData;

    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if(cmd == OMX_CommandStateSet)
    {
        /***************************/
        /* Current State is Loaded */
        /***************************/
        if(m_state == OMX_StateLoaded)
        {
            if(eState == OMX_StateIdle)
            {
                if (allocate_done()||
                    (m_inp_bEnabled == OMX_FALSE && m_out_bEnabled == OMX_FALSE))
                {
                    DEBUG_PRINT("SCP: Loaded->Idle\n");
                }
                else
                {
                    DEBUG_PRINT("SCP: Loaded-->Idle-Pending\n");
                    BITMASK_SET(&m_flags, OMX_COMPONENT_IDLE_PENDING);
                    bFlag = 0;
                }
            }
            else if(eState == OMX_StateLoaded)
            {
                DEBUG_PRINT("OMXCORE-SM: Loaded-->Loaded\n");
                m_cb.EventHandler(&this->m_cmp,
                this->m_app_data,
                OMX_EventError,
                OMX_ErrorSameState,
                OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorSameState;
            }
            else if(eState == OMX_StateWaitForResources)
            {
                DEBUG_PRINT("OMXCORE-SM: Loaded-->WaitForResources\n");
                eRet = OMX_ErrorNone;
            }
            else if(eState == OMX_StateExecuting)
            {
                DEBUG_PRINT("OMXCORE-SM: Loaded-->Executing\n");
                m_cb.EventHandler(&this->m_cmp,
                this->m_app_data,
                OMX_EventError,
                OMX_ErrorIncorrectStateTransition,
                OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            }
            else if(eState == OMX_StatePause)
            {
                DEBUG_PRINT("OMXCORE-SM: Loaded-->Pause\n");
                m_cb.EventHandler(&this->m_cmp,
                this->m_app_data,
                OMX_EventError,
                OMX_ErrorIncorrectStateTransition,
                OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            }
            else if(eState == OMX_StateInvalid)
            {
                DEBUG_PRINT("OMXCORE-SM: Loaded-->Invalid\n");
                m_cb.EventHandler(&this->m_cmp,
                this->m_app_data,
                OMX_EventError,
                OMX_ErrorInvalidState,
                OMX_COMPONENT_GENERATE_EVENT, NULL );
                m_state = OMX_StateInvalid;
                eRet = OMX_ErrorInvalidState;
            }
            else
            {
                DEBUG_PRINT_ERROR("SCP: Loaded-->Invalid(%d Not Handled)\n",
                eState);
                eRet = OMX_ErrorBadParameter;
            }
        }

        /***************************/
        /* Current State is IDLE */
        /***************************/
        else if(m_state == OMX_StateIdle)
        {
            if(eState == OMX_StateLoaded)
            {
                if(release_done(OMX_ALL))
                {
                    if(0 > ioctl(m_drv_fd, AUDIO_ABORT_GET_EVENT, NULL))
                    {
                        DEBUG_PRINT("\n Error in ioctl \
                                AUDIO_ABORT_GET_EVENT\n");
                        return OMX_ErrorHardware;
                    }
                    if (m_ipc_to_event_th != NULL)
                    {
                        omx_aac_thread_stop(m_ipc_to_event_th);
                        m_ipc_to_event_th = NULL;
                    }
                    if(ioctl(m_drv_fd, AUDIO_STOP, 0) == -1)
                    {
                        DEBUG_PRINT("SCP:Idle->Loaded,ioctl stop failed %d\n",
                                    errno);
                    }
                    DEBUG_PRINT("Close device in Send Command Proxy\n");
                    m_first_aac_header= 0;
                    DEBUG_PRINT("SCP: Idle-->Loaded\n");
                }
                else
                {
                    DEBUG_PRINT("SCP--> Idle to Loaded-Pending\n");
                    BITMASK_SET(&m_flags, OMX_COMPONENT_LOADING_PENDING);
                    // Skip the event notification
                    bFlag = 0;
                }
            }
            else if(eState == OMX_StateExecuting)
            {
                if(!m_ipc_to_event_th)
                {
                    DEBUG_PRINT("CREATING EVENT THREAD -->GNG TO EXE STATE");
                    m_ipc_to_event_th = omx_aac_event_thread_create(
                    process_event_cb, this,
                    (char *)"EVENT_THREAD");
                    if(!m_ipc_to_event_th)
                    {
                        DEBUG_PRINT_ERROR("ERROR!!! EVENT THREAD failed to get \
                                            created\n");
                        sem_post (&sem_States);
                        return OMX_ErrorHardware;
                    }
                }

                DEBUG_PRINT("SCP: Idle-->Executing\n");
            }
            else if(eState == OMX_StateIdle)
            {
                DEBUG_PRINT("SCP: Idle-->Idle\n");
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                        OMX_EventError,
                        OMX_ErrorSameState,
                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorSameState;
            }
            else if(eState == OMX_StateWaitForResources)
            {
                DEBUG_PRINT("SCP: Idle-->WaitForResources\n");
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                        OMX_EventError,
                        OMX_ErrorIncorrectStateTransition,
                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            }
            else if(eState == OMX_StatePause)
            {
                DEBUG_PRINT("SCP: Idle-->Pause\n");
            }
            else if(eState == OMX_StateInvalid)
            {
                DEBUG_PRINT("SCP: Idle-->Invalid\n");
                this->m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                        OMX_EventError, OMX_ErrorInvalidState,
                    OMX_COMPONENT_GENERATE_EVENT, NULL );
                m_state = OMX_StateInvalid;
                eRet = OMX_ErrorInvalidState;
            }
            else
            {
                DEBUG_PRINT_ERROR("SCP: Idle --> %d Not Handled\n",eState);
                eRet = OMX_ErrorBadParameter;
            }
        }

        /******************************/
        /* Current State is Executing */
        /******************************/
        else if(m_state == OMX_StateExecuting)
        {
            if(eState == OMX_StateIdle)
            {
                DEBUG_PRINT("SCP: Executing to Idle \n");
                bFlushinprogress = 1;
                m_to_idle = true;
                if(pcm_feedback)
                {
                    execute_omx_flush(OMX_ALL, false); // Flush all ports
                }
                else
                {
                    execute_omx_flush(0,false);
                }
                bFlushinprogress = 0;
                set_eos_bm(0);
            }
            else if(eState == OMX_StatePause)
            {
                /* Issue pause to driver to pause tunnel mode playback */
                if(!pcm_feedback) {
                    ioctl(m_drv_fd,AUDIO_PAUSE, 1);
                }
                DEBUG_DETAIL("*************************\n");
                DEBUG_PRINT("SCP-->RXED PAUSE STATE\n");
                DEBUG_DETAIL("*************************\n");
            }
            else if(eState == OMX_StateLoaded)
            {
                DEBUG_PRINT("\n SCP:Executing --> Loaded \n");
                this->m_cb.EventHandler(&this->m_cmp,
                                        this->m_app_data,
                                        OMX_EventError,
                                        OMX_ErrorIncorrectStateTransition,
                                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            }
            else if(eState == OMX_StateWaitForResources)
            {
                DEBUG_PRINT("\nSCP:Executing --> WaitForResources \n");
                this->m_cb.EventHandler(&this->m_cmp,
                                        this->m_app_data,
                                        OMX_EventError,
                                        OMX_ErrorIncorrectStateTransition,
                                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            }
            else if(eState == OMX_StateExecuting)
            {
                DEBUG_PRINT("\n SCP: Executing --> Executing \n");
                this->m_cb.EventHandler(&this->m_cmp,
                                        this->m_app_data,
                                        OMX_EventError,
                                        OMX_ErrorSameState,
                                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorSameState;
            }
            else if(eState == OMX_StateInvalid)
            {
                DEBUG_PRINT("\n SCP: Executing --> Invalid \n");
                this->m_cb.EventHandler(&this->m_cmp,
                                        this->m_app_data,
                                        OMX_EventError,
                                        OMX_ErrorInvalidState,
                                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                m_state = OMX_StateInvalid;
                eRet = OMX_ErrorInvalidState;
            }
            else
            {
                DEBUG_PRINT_ERROR("SCP: Executing --> %d Not Handled\n",eState);
                eRet = OMX_ErrorBadParameter;
            }
        }
        /***************************/
        /* Current State is Pause  */
        /***************************/
        else if(m_state == OMX_StatePause)
        {
            DEBUG_PRINT("SCP: Paused --> Executing in=%d out%d\n",
                is_in_th_sleep, is_out_th_sleep);
            if( (eState == OMX_StateExecuting || eState == OMX_StateIdle) )
            {
                if(is_in_th_sleep)
                {
                    is_in_th_sleep = false;
                    DEBUG_DETAIL("SCP: WAKING UP IN THREAD\n");
                    in_th_wakeup();
                }
                if(is_out_th_sleep)
                {
                    DEBUG_DETAIL("SCP: WAKING UP OUT THREAD\n");
                    out_th_wakeup();
                    is_out_th_sleep = false;
                }
            }

            if(eState == OMX_StateExecuting)
            {
                DEBUG_PRINT("SCP: Paused --> Executing\n");
                /* Issue pause to driver to pause tunnel mode playback */
                if(!pcm_feedback)
                {
                    ioctl(m_drv_fd,AUDIO_PAUSE, 0);
                }
            }
            else if(eState == OMX_StateIdle)
            {
                m_to_idle = true;
                DEBUG_PRINT("SCP: Paused to Idle \n");
                DEBUG_PRINT ("\n Internal flush issued");
                bFlushinprogress = 1;
                if(pcm_feedback)
                {
                    execute_omx_flush(OMX_ALL, false); // Flush all ports
                }
                else
                {
                    execute_omx_flush(0,false);
                }
                bFlushinprogress = 0;
                set_eos_bm(0);
            }
            else if(eState == OMX_StateLoaded)
            {
                DEBUG_PRINT("\n SCP:Pause --> loaded \n");
                this->m_cb.EventHandler(&this->m_cmp,
                                        this->m_app_data,
                                        OMX_EventError,
                                        OMX_ErrorIncorrectStateTransition,
                                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            }
            else if(eState == OMX_StateWaitForResources)
            {
                DEBUG_PRINT("\n SCP: Pause --> WaitForResources \n");
                this->m_cb.EventHandler(&this->m_cmp,
                                        this->m_app_data,
                                        OMX_EventError,
                                        OMX_ErrorIncorrectStateTransition,
                                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            }
            else if(eState == OMX_StatePause)
            {
                DEBUG_PRINT("\n SCP:Pause --> Pause \n");
                this->m_cb.EventHandler(&this->m_cmp,
                                        this->m_app_data,
                                        OMX_EventError,
                                        OMX_ErrorSameState,
                                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorSameState;
            }
            else if(eState == OMX_StateInvalid)
            {
                DEBUG_PRINT("\n SCP:Pause --> Invalid \n");
                this->m_cb.EventHandler(&this->m_cmp,
                                        this->m_app_data,
                                        OMX_EventError,
                                        OMX_ErrorInvalidState,
                                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                m_state = OMX_StateInvalid;
                eRet = OMX_ErrorInvalidState;
            }
            else
            {
                DEBUG_PRINT("SCP: Paused to %d Not Handled\n",eState);
                eRet = OMX_ErrorBadParameter;
            }
        }
        /**************************************/
        /* Current State is WaitForResources  */
        /**************************************/
        else if(m_state == OMX_StateWaitForResources)
        {
            if(eState == OMX_StateLoaded)
            {
                DEBUG_PRINT("SCP: WaitForResources-->Loaded\n");
            }
            else if(eState == OMX_StateWaitForResources)
            {
                DEBUG_PRINT("SCP: WaitForResources-->WaitForResources\n");
                this->m_cb.EventHandler(&this->m_cmp,
                                        this->m_app_data,
                                        OMX_EventError,
                                        OMX_ErrorSameState,
                                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorSameState;
            }
            else if(eState == OMX_StateExecuting)
            {
                DEBUG_PRINT("SCP: WaitForResources-->Executing\n");
                this->m_cb.EventHandler(&this->m_cmp,
                                        this->m_app_data,
                                        OMX_EventError,
                                        OMX_ErrorIncorrectStateTransition,
                                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            }
            else if(eState == OMX_StatePause)
            {
                DEBUG_PRINT("SCP: WaitForResources-->Pause\n");
                this->m_cb.EventHandler(&this->m_cmp,
                                        this->m_app_data,
                                        OMX_EventError,
                                        OMX_ErrorIncorrectStateTransition,
                                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                eRet = OMX_ErrorIncorrectStateTransition;
            }
            else if(eState == OMX_StateInvalid)
            {
                DEBUG_PRINT("SCP: WaitForResources-->Invalid\n");
                this->m_cb.EventHandler(&this->m_cmp,
                                        this->m_app_data,
                                        OMX_EventError,
                                        OMX_ErrorInvalidState,
                                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                m_state = OMX_StateInvalid;
                eRet = OMX_ErrorInvalidState;
            }
            else
            {
                DEBUG_PRINT_ERROR("SCP--> %d to %d(Not Handled)\n",
                m_state,eState);
                eRet = OMX_ErrorBadParameter;
            }
        }

        /****************************/
        /* Current State is Invalid */
        /****************************/
        else if(m_state == OMX_StateInvalid)
        {
            if(OMX_StateLoaded == eState || OMX_StateWaitForResources == eState
            || OMX_StateIdle == eState || OMX_StateExecuting == eState
            || OMX_StatePause == eState || OMX_StateInvalid == eState)
            {
                DEBUG_PRINT("SCP: Invalid-->Loaded/Idle/Executing/Pause/" \
                "Invalid/WaitForResources");
                this->m_cb.EventHandler(&this->m_cmp,
                                        this->m_app_data,
                                        OMX_EventError,
                                        OMX_ErrorInvalidState,
                                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                m_state = OMX_StateInvalid;
                eRet = OMX_ErrorInvalidState;
            }
            else
            {
                DEBUG_PRINT("SCP: Paused --> %d Not Handled\n",eState);
                eRet = OMX_ErrorBadParameter;
            }
        }
    }
    else if (cmd == OMX_CommandFlush)
    {
        DEBUG_DETAIL("*************************\n");
        DEBUG_PRINT("SCP-->RXED FLUSH COMMAND port=%d m_state=%d\n",(signed)param1,m_state);
        DEBUG_DETAIL("*************************\n");

        bFlushinprogress = 1;


        bFlag = 0;
        if((param1 == OMX_CORE_INPUT_PORT_INDEX) ||
                (param1 == OMX_CORE_OUTPUT_PORT_INDEX) ||
                (param1 == OMX_ALL))
        {
	    if(m_state == OMX_StateInvalid) {
                execute_omx_flush(param1, false);
                DEBUG_PRINT_ERROR("component in invalid state do not send flush cmpl"\
				"event\n");
	    } else
                execute_omx_flush(param1);
        }
        else
        {
            eRet = OMX_ErrorBadPortIndex;
            m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventError,
            OMX_CommandFlush, OMX_ErrorBadPortIndex, NULL );
        }

        if((get_eos_bm() & IP_OP_PORT_BITMASK)== IP_OP_PORT_BITMASK)
        {
            set_eos_bm(0);
        }
        if(((get_eos_bm()) & IP_PORT_BITMASK) &&
           ((param1 == OMX_CORE_INPUT_PORT_INDEX) ||
            (param1 == OMX_ALL)))
        {
            set_eos_bm(0);
        }
        bFlushinprogress = 0;
    }
    else if (cmd == OMX_CommandPortDisable)
    {
        // Skip the event notification as it is done in below if loop
        bFlag = 0;
        if(param1 == OMX_CORE_INPUT_PORT_INDEX || param1 == OMX_ALL)
        {
            DEBUG_PRINT("SCP: Disabling Input port Indx\n");
            m_inp_bEnabled = OMX_FALSE;
            if(((m_state == OMX_StateLoaded) || (m_state == OMX_StateIdle))
                    && release_done(0))
            {
                bOutputPortReEnabled = 0;
                DEBUG_PRINT("send_command_proxy:OMX_CommandPortDisable:\
                            OMX_CORE_INPUT_PORT_INDEX:release_done \n");
                DEBUG_PRINT("************* OMX_CommandPortDisable:\
                            m_inp_bEnabled si %d\n",m_inp_bEnabled);

                post_command(OMX_CommandPortDisable,
                OMX_CORE_INPUT_PORT_INDEX,OMX_COMPONENT_GENERATE_EVENT);
            }
            else
            {
                if((m_state == OMX_StatePause) ||
                (m_state == OMX_StateExecuting))
                {
                    DEBUG_PRINT("SCP: execute_omx_flush in Disable in \
                                param1=%d m_state=%d \n",(signed)param1,
                                m_state);
                    bFlushinprogress = 1;
                    execute_omx_flush(param1,false );
                    bFlushinprogress = 0;
                }
                DEBUG_PRINT("send_command_proxy:OMX_CommandPortDisable:\
                            OMX_CORE_INPUT_PORT_INDEX \n");
                BITMASK_SET(&m_flags, OMX_COMPONENT_INPUT_DISABLE_PENDING);
            }

        }
        if ((param1 == OMX_CORE_OUTPUT_PORT_INDEX) || (param1 == OMX_ALL))
        {
            DEBUG_PRINT("SCP: Disabling Output port Indx\n");
            m_out_bEnabled = OMX_FALSE;
            if(((m_state == OMX_StateLoaded) || (m_state == OMX_StateIdle))
                    && release_done(1))
            {
                DEBUG_PRINT("send_command_proxy:OMX_CommandPortDisable:\
                            OMX_CORE_OUTPUT_PORT_INDEX:release_done \n");
                DEBUG_PRINT("************* OMX_CommandPortDisable:\
                            m_out_bEnabled is %d\n",m_inp_bEnabled);

                post_command(OMX_CommandPortDisable,
                OMX_CORE_OUTPUT_PORT_INDEX,
                OMX_COMPONENT_GENERATE_EVENT);
            }

            else
            {
                if((m_state == OMX_StatePause) ||
                (m_state == OMX_StateExecuting))
                {
                    DEBUG_PRINT("SCP: execute_omx_flush in Disable out \
                                param1=%d m_state=%d \n",(signed)param1,
                                m_state);
                    bFlushinprogress = 1;
                    execute_omx_flush(param1, false);
                    bFlushinprogress = 0;
                }
                BITMASK_SET(&m_flags, OMX_COMPONENT_OUTPUT_DISABLE_PENDING);
            }
        }
        else
        {
            DEBUG_PRINT_ERROR("SCP-->Disabling invalid port ID[%d]",
                (signed)param1);
        }
    }
    else if (cmd == OMX_CommandPortEnable)
    {
        // Skip the event notification as it is done in below if loop
        bFlag = 0;
        if (param1 == OMX_CORE_INPUT_PORT_INDEX  || param1 == OMX_ALL)
        {
            m_inp_bEnabled = OMX_TRUE;
            DEBUG_PRINT("SCP: Enabling Input port Indx\n");
            if(((m_state == OMX_StateLoaded)
            && !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
                    || (m_state == OMX_StateWaitForResources)
                    || (m_inp_bPopulated == OMX_TRUE))
            {
                post_command(OMX_CommandPortEnable,
                OMX_CORE_INPUT_PORT_INDEX,OMX_COMPONENT_GENERATE_EVENT);
            }
            else
            {
                BITMASK_SET(&m_flags, OMX_COMPONENT_INPUT_ENABLE_PENDING);
            }
        }

        if ((param1 == OMX_CORE_OUTPUT_PORT_INDEX) || (param1 == OMX_ALL))
        {
            bOutputPortReEnabled = 1;
            DEBUG_PRINT("SCP: Enabling Output port Indx\n");
            m_out_bEnabled = OMX_TRUE;
            if(((m_state == OMX_StateLoaded)
                    && !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
                    || (m_state == OMX_StateWaitForResources)
                    || (m_out_bPopulated == OMX_TRUE))
            {
                post_command(OMX_CommandPortEnable,
                OMX_CORE_OUTPUT_PORT_INDEX,OMX_COMPONENT_GENERATE_EVENT);
            }
            else
            {
                DEBUG_PRINT("send_command_proxy:OMX_CommandPortEnable:\
                            OMX_CORE_OUTPUT_PORT_INDEX:release_done \n");
                bOutputPortReEnabled = 0;
                BITMASK_SET(&m_flags, OMX_COMPONENT_OUTPUT_ENABLE_PENDING);
            }
            if(is_in_th_sleep)
            {
                is_in_th_sleep = false;
                DEBUG_DETAIL("SCP:WAKING UP IN THREADS\n");
                in_th_wakeup();
            }
            if(is_out_th_sleep)
            {
                is_out_th_sleep = false;
                DEBUG_PRINT("SCP:WAKING OUT THR, OMX_CommandPortEnable\n");
                out_th_wakeup();
            }
        }
        else
        {
            DEBUG_PRINT_ERROR("SCP-->Enabling invalid port ID[%d]",(
                    signed)param1);
        }
    }
    else
    {
        DEBUG_PRINT_ERROR("SCP-->ERROR: Invali Command [%d]\n",cmd);
        eRet = OMX_ErrorNotImplemented;
    }

    DEBUG_PRINT("posting sem_States\n");
    sem_post (&sem_States);

    if(eRet == OMX_ErrorNone && bFlag)
    {
        post_command(cmd,eState,OMX_COMPONENT_GENERATE_EVENT);
    }
    return eRet;
}

/**
@brief member function that flushes buffers that are pending to be written
to driver

@param none
@return bool value indicating whether flushing is carried out successfully
*/
bool COmxAacDec::execute_omx_flush(OMX_IN OMX_U32 param1, bool cmd_cmpl)
{
    bool bRet = true;

    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    DEBUG_PRINT("Execute_omx_flush Port[%d]",(signed) param1);
    if (param1 == OMX_ALL)
    {
        bFlushinprogress = true;
        DEBUG_PRINT("Execute flush for both I/p O/p port\n");
        pthread_mutex_lock(&m_flush_lock);
        m_flush_cnt = 2;
        pthread_mutex_unlock(&m_flush_lock);

        // Send Flush commands to input and output threads
        post_input(OMX_CommandFlush,
        OMX_CORE_INPUT_PORT_INDEX,OMX_COMPONENT_GENERATE_COMMAND);

        post_output(OMX_CommandFlush,
        OMX_CORE_OUTPUT_PORT_INDEX,OMX_COMPONENT_GENERATE_COMMAND);
        DEBUG_PRINT("AUDIO FLUSH issued\n");
        // Send Flush to the kernel so that the in and out buffers are released
        if(ioctl( m_drv_fd, AUDIO_FLUSH, 0) == -1)
            DEBUG_PRINT("FLush:ioctl flush failed errno=%d\n",errno);

        in_th_wakeup();
        out_th_wakeup();

        while (1 )
        {
            pthread_mutex_lock(&out_buf_count_lock);
            pthread_mutex_lock(&in_buf_count_lock);
            DEBUG_PRINT("Flush:nNumOutputBuf = %d nNumInputBuf=%d\n",\
            nNumOutputBuf,nNumInputBuf);
            DEBUG_PRINT("Flush:drv_out_buf_cnt=%d drv_inp_buf_cnt=%d\n",\
            drv_out_buf_cnt,drv_inp_buf_cnt);
            if(drv_inp_buf_cnt > 0 || drv_out_buf_cnt > 0)
            {
                pthread_mutex_unlock(&in_buf_count_lock);
                pthread_mutex_unlock(&out_buf_count_lock);
                in_th_wakeup();
                out_th_wakeup();
            DEBUG_PRINT("AUDIO FLUSH issued second time \
                         for any recently sent buffers\n");
                // Send Flush to the kernel so that the in and out buffers are released
                if(ioctl( m_drv_fd, AUDIO_FLUSH, 0) == -1)
                     DEBUG_PRINT("FLush:ioctl flush failed errno=%d\n",errno);
                usleep (10000);
            }
            else
            {
                pthread_mutex_unlock(&in_buf_count_lock);
                pthread_mutex_unlock(&out_buf_count_lock);
                break;
            }
        }
        DEBUG_PRINT("RECIEVED BOTH FLUSH ACK's param1=%d cmd_cmpl=%d",\
        (signed)param1,cmd_cmpl);

        // sleep till the FLUSH ACK are done by both the input and
        // output threads
        DEBUG_DETAIL("WAITING FOR FLUSH ACK's param1=%d",(signed)param1);
        wait_for_event();

        // If not going to idle state, Send FLUSH complete message to the Client,
        // now that FLUSH ACK's have been recieved.
        if(cmd_cmpl)
        {
            m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventCmdComplete,
            OMX_CommandFlush, OMX_CORE_INPUT_PORT_INDEX, NULL );
            m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventCmdComplete,
            OMX_CommandFlush, OMX_CORE_OUTPUT_PORT_INDEX, NULL );
            DEBUG_PRINT("Inside FLUSH.. sending FLUSH CMPL\n");
        }
        //Reset timestamp for adif clips on flush to prevent timestamp overflow.
        if(adif)
        {
            nTimestamp = 0;
        }
        bFlushinprogress = false;
    }
    else if (param1 == OMX_CORE_INPUT_PORT_INDEX)
    {
        DEBUG_PRINT("Execute FLUSH for I/p port\n");
        bFlushinprogress = true;
        pthread_mutex_lock(&m_flush_lock);
        m_flush_cnt = 1;
        pthread_mutex_unlock(&m_flush_lock);
        post_input(OMX_CommandFlush,
        OMX_CORE_INPUT_PORT_INDEX,OMX_COMPONENT_GENERATE_COMMAND);
        if(ioctl( m_drv_fd, AUDIO_FLUSH, 0) == -1)
        {
            DEBUG_PRINT("FLush:ioctl flush failed errno=%d\n",errno);
        }
        in_th_wakeup();
        DEBUG_DETAIL("WAITING FOR FLUSH ACK's param1=%d",(signed)param1);
        wait_for_event();
        DEBUG_DETAIL(" RECIEVED FLUSH ACK FOR I/P PORT param1=%d",
            (signed)param1);

        // Send FLUSH complete message to the Client,
        // now that FLUSH ACK's have been recieved.
        if(cmd_cmpl)
        {
            m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventCmdComplete,
            OMX_CommandFlush, OMX_CORE_INPUT_PORT_INDEX, NULL );
        }
        DEBUG_DETAIL("RECIEVED FLUSH ACK FOR I/P PORT param1=%d ",
            (signed)param1);
        //Reset timestamp for adif clips on flush to prevent timestamp overflow.
        if(adif)
        {
            nTimestamp = 0;
        }
        bFlushinprogress = false;
    }
    else if (param1 == OMX_CORE_OUTPUT_PORT_INDEX)
    {
        DEBUG_PRINT("Executing FLUSH for O/p port\n");
        bFlushinprogress = true;
        pthread_mutex_lock(&m_flush_lock);
        m_flush_cnt = 1;
        pthread_mutex_unlock(&m_flush_lock);

        DEBUG_DETAIL("Executing FLUSH for O/p port\n");
        post_output(OMX_CommandFlush,
                    OMX_CORE_OUTPUT_PORT_INDEX,OMX_COMPONENT_GENERATE_COMMAND);
        // Driver does not have output flush support explicit
        // using general flush command which is applicable for both
        if(ioctl( m_drv_fd, AUDIO_OUTPORT_FLUSH, 0) == -1)
            DEBUG_PRINT("FLush:ioctl flush failed errno=%d\n",errno);
        out_th_wakeup();

        // sleep till the FLUSH ACK are done by both the input and output thrds
        wait_for_event();
        // Send FLUSH complete message to the Client,
        // now that FLUSH ACK's have been recieved.
        if(cmd_cmpl){
            m_cb.EventHandler(&m_cmp, m_app_data, OMX_EventCmdComplete,
            OMX_CommandFlush, OMX_CORE_OUTPUT_PORT_INDEX, NULL );
        }
        DEBUG_DETAIL("RECIEVED FLUSH ACK FOR O/P PORT param1=%d ",
            (signed)param1);
        //Reset timestamp for adif clips on flush to prevent timestamp overflow.
        if(adif)
        {
            nTimestamp = 0;
        }
        bFlushinprogress = false;
    }
    else
    {
        DEBUG_PRINT("Invalid Port ID[%d]",(signed)param1);

    }
    return bRet;
}

/**
@brief member function that flushes buffers that are pending to be written
to driver

@param none
@return bool value indicating whether flushing is carried out successfully
*/
bool COmxAacDec::execute_input_omx_flush()
{
    OMX_BUFFERHEADERTYPE *omx_buf;
    unsigned      p1; // Parameter - 1
    unsigned      p2; // Parameter - 2
    unsigned      ident;
    unsigned       qsize=0; // qsize
    unsigned       tot_qsize=0; // qsize

    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    DEBUG_PRINT("Execute_omx_flush on input port");

    do
    {
        pthread_mutex_lock(&m_inputlock);
        qsize = m_din_q.m_size;
        tot_qsize = qsize;
        tot_qsize += m_ebd_q.m_size;

        pthread_mutex_lock(&in_buf_count_lock);
        tot_qsize += drv_inp_buf_cnt;
        pthread_mutex_unlock(&in_buf_count_lock);

        DEBUG_DETAIL("Input FLUSH-->flushq[%d] ebd[%d]dataq[%d]",\
        m_cin_q.m_size,
        m_ebd_q.m_size,qsize);

        if(!tot_qsize)
        {
            DEBUG_DETAIL("Input-->BREAKING FROM execute_input_flush LOOP");
            pthread_mutex_unlock(&m_inputlock);
            break;
        }
        if (qsize)
        {
            m_din_q.pop_entry(&p1, &p2, &ident);
            pthread_mutex_unlock(&m_inputlock);
            if ((ident == OMX_COMPONENT_GENERATE_ETB))
            {
                omx_buf = (OMX_BUFFERHEADERTYPE *) p2;
                DEBUG_DETAIL("Flush:Input dataq=0x%x \n", (unsigned)omx_buf);
                omx_buf->nFilledLen = 0;
                buffer_done_cb((OMX_BUFFERHEADERTYPE *)omx_buf, true);
            }
        }
        else if((m_ebd_q.m_size))
        {
            m_ebd_q.pop_entry(&p1, &p2, &ident);
	    pthread_mutex_unlock(&m_inputlock);
            if(ident == OMX_COMPONENT_GENERATE_BUFFER_DONE)
            {
                omx_buf = (OMX_BUFFERHEADERTYPE *) p2;
                omx_buf->nFilledLen = 0;
                DEBUG_DETAIL("Flush:ctrl dataq=0x%x \n",(unsigned) omx_buf);
                buffer_done_cb((OMX_BUFFERHEADERTYPE *)omx_buf);
            }
        }
        else
        {
		pthread_mutex_unlock(&m_inputlock);
                if(drv_inp_buf_cnt)
                {
                    usleep(100); //Wait for driver input buffer to be free
                    DEBUG_PRINT("\n i/p Flush: drv_inp_buf_cnt is %d state %x\n",
				drv_inp_buf_cnt, m_state);
                }
        }
    }while(tot_qsize>0);
    DEBUG_DETAIL("*************************\n");
    DEBUG_DETAIL("IN-->FLUSHING DONE\n");
    DEBUG_DETAIL("*************************\n");
    flush_ack();
    return true;
}
/**
@brief member function that flushes buffers that are pending to be written
to driver

@param none
@return bool value indicating whether flushing is carried out successfully
*/
bool COmxAacDec::execute_output_omx_flush()
{
    OMX_BUFFERHEADERTYPE *omx_buf;
    unsigned      p1; // Parameter - 1
    unsigned      p2; // Parameter - 2
    unsigned      ident;
    unsigned       qsize=0; // qsize
    unsigned       tot_qsize=0; // qsize
    DEBUG_PRINT("Execute_omx_flush on output port");
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    do
    {
        pthread_mutex_lock(&m_outputlock);
        qsize = m_dout_q.m_size;
        DEBUG_DETAIL("OUT FLUSH-->flushq[%d] fbd[%d]dataq[%d]",\
        m_cout_q.m_size,
        m_fbd_q.m_size,qsize);
        tot_qsize = qsize;
        tot_qsize += m_fbd_q.m_size;

        pthread_mutex_lock(&out_buf_count_lock);
        tot_qsize += drv_out_buf_cnt;
        pthread_mutex_unlock(&out_buf_count_lock);

        if(!tot_qsize)
        {
            DEBUG_DETAIL("OUT-->BREAKING FROM execute_output_flush LOOP");
            pthread_mutex_unlock(&m_outputlock);
            break;
        }
        if (qsize)
        {
            m_dout_q.pop_entry(&p1,&p2,&ident);
            pthread_mutex_unlock(&m_outputlock);
            if((ident == OMX_COMPONENT_GENERATE_FTB))
            {
                omx_buf = (OMX_BUFFERHEADERTYPE *) p2;
                DEBUG_DETAIL("Ouput Buf_Addr=%x TS[%lld] \n",\
                (unsigned)omx_buf, nTimestamp);
                omx_buf->nTimeStamp = nTimestamp;
                omx_buf->nFilledLen = 0;
		omx_buf->nOffset = 0;
                frame_done_cb((OMX_BUFFERHEADERTYPE *)omx_buf, true);
                DEBUG_DETAIL("CALLING FBD FROM FLUSH");
            }
        }
        else if((qsize = m_fbd_q.m_size))
        {
            m_fbd_q.pop_entry(&p1, &p2, &ident);
            pthread_mutex_unlock(&m_outputlock);
            if(ident == OMX_COMPONENT_GENERATE_FRAME_DONE)
            {
                omx_buf = (OMX_BUFFERHEADERTYPE *) p2;
                DEBUG_DETAIL("Ouput Buf_Addr=%x TS[%lld] \n", \
                (unsigned) omx_buf,nTimestamp);
                omx_buf->nTimeStamp = nTimestamp;
                omx_buf->nFilledLen = 0;
		omx_buf->nOffset = 0;
                frame_done_cb((OMX_BUFFERHEADERTYPE *)omx_buf);
                DEBUG_DETAIL("CALLING FROM CTRL-FBDQ FROM FLUSH");
            }
        }
        else
        {
            pthread_mutex_unlock(&m_outputlock);
            if(drv_out_buf_cnt)
            {
               usleep(100); //Wait for driver output buffer to be free
               DEBUG_PRINT("\n o/p Flush: drv_out_buf_cnt is %d state %x\n",
				drv_out_buf_cnt, m_state);
            }
        }
    }while(tot_qsize>0);
    DEBUG_DETAIL("*************************\n");
    DEBUG_DETAIL("OUT-->FLUSHING DONE\n");
    DEBUG_DETAIL("*************************\n");
    flush_ack();
    return true;
}

/**
@brief member function that posts command
in the command queue

@param p1 first paramter for the command
@param p2 second parameter for the command
@param id command ID
@param lock self-locking mode
@return bool indicating command being queued
*/
bool COmxAacDec::post_input(unsigned int p1,
                            unsigned int p2,
                            unsigned int id)
{
    bool bRet = false;
    pthread_mutex_lock(&m_inputlock);

    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if((id == OMX_COMPONENT_SUSPEND) ||
       (OMX_COMPONENT_GENERATE_COMMAND == id))
    {
        m_cin_q.insert_entry(p1,p2,id);
    }
    else if((OMX_COMPONENT_GENERATE_BUFFER_DONE == id))
    {
        m_ebd_q.insert_entry(p1,p2,id);
    }
    else
    {
        m_din_q.insert_entry(p1,p2,id);
    }
    if(m_ipc_to_in_th)
    {
        bRet = true;
        omx_aac_post_msg(m_ipc_to_in_th, id);
    }

    DEBUG_DETAIL("PostInput-->state[%d]id[%d]cin[%d]ebdq[%d]dataq[%d]\n",\
    m_state,
    id,
    m_cin_q.m_size,
    m_ebd_q.m_size,
    m_din_q.m_size);

    pthread_mutex_unlock(&m_inputlock);

    return bRet;
}

bool COmxAacDec::post_command(unsigned int p1,
                            unsigned int p2,
                            unsigned int id)
{
    bool bRet = false;
    pthread_mutex_lock(&m_commandlock);

    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    m_cmd_q.insert_entry(p1,p2,id);
    if(m_ipc_to_cmd_th)
    {
        bRet = true;
        omx_aac_post_msg(m_ipc_to_cmd_th, id);
    }

    DEBUG_DETAIL("PostCmd-->state[%d]id[%d]cmdq[%d]",
    m_state,
    id,
    m_cmd_q.m_size);

    pthread_mutex_unlock(&m_commandlock);
    return bRet;
}


bool COmxAacDec::post_output(unsigned int p1,
                            unsigned int p2,
                            unsigned int id)
{
    bool bRet = false;
    pthread_mutex_lock(&m_outputlock);

    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if((id == OMX_COMPONENT_SUSPEND) || (id == OMX_COMPONENT_RESUME) ||
       (OMX_COMPONENT_GENERATE_COMMAND == id))
    {
        m_cout_q.insert_entry(p1,p2,id);
    }
    else if((OMX_COMPONENT_GENERATE_FRAME_DONE == id) )
    {
        m_fbd_q.insert_entry(p1,p2,id);
    }
    else
    {
        m_dout_q.insert_entry(p1,p2,id);
    }
    if(m_ipc_to_out_th)
    {
        bRet = true;
        omx_aac_post_msg(m_ipc_to_out_th, id);
    }
    DEBUG_DETAIL("PostOutput-->state[%d]id[%d]flushq[%d]fbdq[%d]dataq[%d]\n",\
    m_state,
    id,
    m_cout_q.m_size,
    m_fbd_q.m_size,
    m_dout_q.m_size);

    pthread_mutex_unlock(&m_outputlock);

    return bRet;
}
/**
@brief member function that return parameters to IL client

@param hComp handle to component instance
@param paramIndex Parameter type
@param paramData pointer to memory space which would hold the
        paramter
@return error status
*/
OMX_ERRORTYPE  COmxAacDec::get_parameter(OMX_IN OMX_HANDLETYPE hComp,
                                         OMX_IN OMX_INDEXTYPE paramIndex,
                                         OMX_INOUT OMX_PTR     paramData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Get Param in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    if(paramData == NULL)
    {
        DEBUG_PRINT("get_parameter: paramData is NULL\n");
        return OMX_ErrorBadParameter;
    }
    switch(paramIndex)
    {
    case OMX_IndexParamPortDefinition:
        {
            OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
            portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;

            DEBUG_PRINT("OMX_IndexParamPortDefinition PortIndex = %d\n",\
                (int)portDefn->nPortIndex);

            portDefn->nVersion.nVersion = OMX_SPEC_VERSION;
            portDefn->nSize = sizeof(portDefn);
            portDefn->eDomain    = OMX_PortDomainAudio;

            if (0 == portDefn->nPortIndex)
            {
                portDefn->eDir       = OMX_DirInput;
                portDefn->bEnabled   = m_inp_bEnabled;
                portDefn->bPopulated = m_inp_bPopulated;
                portDefn->nBufferCountActual = m_inp_act_buf_count;
                portDefn->nBufferCountMin    = OMX_CORE_NUM_INPUT_BUFFERS;
                portDefn->nBufferSize        = input_buffer_size;
                portDefn->format.audio.bFlagErrorConcealment = OMX_TRUE;
                portDefn->format.audio.eEncoding = OMX_AUDIO_CodingAAC;
                portDefn->format.audio.pNativeRender = 0;
            }
            else if (1 == portDefn->nPortIndex)
            {
                portDefn->eDir =  OMX_DirOutput;
                portDefn->bEnabled   = m_out_bEnabled;
                portDefn->bPopulated = m_out_bPopulated;
                portDefn->nBufferCountActual = m_out_act_buf_count;
                portDefn->nBufferCountMin    = OMX_CORE_NUM_OUTPUT_BUFFERS;
                portDefn->nBufferSize        = output_buffer_size;
                portDefn->format.audio.bFlagErrorConcealment = OMX_TRUE;
                portDefn->format.audio.eEncoding = OMX_AUDIO_CodingPCM;
                portDefn->format.audio.pNativeRender = 0;
            }
            else
            {
                portDefn->eDir =  OMX_DirMax;
                DEBUG_PRINT_ERROR("Bad Port idx %d\n",
                (int)portDefn->nPortIndex);
                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }

    case OMX_IndexParamAudioInit:
        {
            OMX_PORT_PARAM_TYPE *portParamType =
            (OMX_PORT_PARAM_TYPE *) paramData;
            DEBUG_PRINT("OMX_IndexParamAudioInit\n");

            portParamType->nVersion.nVersion = OMX_SPEC_VERSION;
            portParamType->nSize = sizeof(portParamType);
            portParamType->nPorts           = 2;
            portParamType->nStartPortNumber = 0;
            break;
        }

    case OMX_IndexParamAudioPortFormat:
        {
            OMX_AUDIO_PARAM_PORTFORMATTYPE *portFormatType =
            (OMX_AUDIO_PARAM_PORTFORMATTYPE *) paramData;
            DEBUG_PRINT("OMX_IndexParamAudioPortFormat\n");

            portFormatType->nVersion.nVersion = OMX_SPEC_VERSION;
            portFormatType->nSize = sizeof(portFormatType);
            if (OMX_CORE_INPUT_PORT_INDEX == portFormatType->nPortIndex)
            {
                portFormatType->eEncoding = OMX_AUDIO_CodingAAC;
            }
            else if(OMX_CORE_OUTPUT_PORT_INDEX== portFormatType->nPortIndex)
            {
                DEBUG_PRINT("get_parameter: OMX_IndexParamAudioFormat: %u\n",
                (unsigned)(portFormatType->nIndex));
                portFormatType->eEncoding = OMX_AUDIO_CodingPCM;
            }
            else
            {
                DEBUG_PRINT_ERROR("get_parameter: Bad port index %d\n",
                (int)portFormatType->nPortIndex);
                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }
    case OMX_IndexParamAudioAac:
        {
            DEBUG_PRINT("GET PARAM-->OMX_IndexParamAudioAac.. \
                SF=%u ch=%u\n",(unsigned int)m_adec_param.nSampleRate,
            (unsigned int)m_adec_param.nChannels);
            OMX_AUDIO_PARAM_AACPROFILETYPE *aacParam =
            (OMX_AUDIO_PARAM_AACPROFILETYPE *) paramData;

            if (OMX_CORE_INPUT_PORT_INDEX== aacParam->nPortIndex)
            {
                memcpy(aacParam,&m_adec_param,
                sizeof(OMX_AUDIO_PARAM_AACPROFILETYPE));
            }
            else
            {
                DEBUG_PRINT_ERROR("get_parameter:OMX_IndexParamAudioAac \
                    OMX_ErrorBadPortIndex %d\n", (int)aacParam->nPortIndex);
                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }

    case QOMX_IndexParamAudioSessionId:
        {
            QOMX_AUDIO_STREAM_INFO_DATA *streaminfoparam =
            (QOMX_AUDIO_STREAM_INFO_DATA *) paramData;
            streaminfoparam->sessionId = m_session_id;
            break;
        }

    case OMX_IndexParamAudioPcm:
        {
            OMX_AUDIO_PARAM_PCMMODETYPE *pcmparam =
            (OMX_AUDIO_PARAM_PCMMODETYPE *) paramData;

            if (OMX_CORE_OUTPUT_PORT_INDEX== pcmparam->nPortIndex)
            {
                pcmparam->eNumData  =    m_pcm_param.eNumData;
                pcmparam->bInterleaved  = m_pcm_param.bInterleaved;
                pcmparam->nBitPerSample = m_pcm_param.nBitPerSample;
                pcmparam->ePCMMode = m_pcm_param.ePCMMode;
                pcmparam->eChannelMapping[0] = m_pcm_param.eChannelMapping[0];
                pcmparam->eChannelMapping[1] = m_pcm_param.eChannelMapping[1];
                pcmparam->nChannels = m_adec_param.nChannels;
                pcmparam->nSamplingRate = m_adec_param.nSampleRate;

                DEBUG_PRINT("get_parameter: Sampling rate %u",
                    (unsigned) pcmparam->nSamplingRate);
                DEBUG_PRINT("get_parameter: Number of channels %u",
                    (unsigned)pcmparam->nChannels);
            }
            else
            {
                DEBUG_PRINT_ERROR("get_parameter:OMX_IndexParamAudioPcm \
                    OMX_ErrorBadPortIndex %u\n",
                    (unsigned) pcmparam->nPortIndex);
                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }
    case OMX_IndexParamComponentSuspended:
        {
            OMX_PARAM_SUSPENSIONTYPE *suspend =
            (OMX_PARAM_SUSPENSIONTYPE *) paramData;
            suspend->eType = OMX_NotSuspended;
            DEBUG_PRINT("get_parameter: suspend type %d", suspend->eType);
            break;
        }
    case OMX_IndexParamPriorityMgmt:
        {
            OMX_PRIORITYMGMTTYPE *priorityMgmtType =
            (OMX_PRIORITYMGMTTYPE*)paramData;
            DEBUG_PRINT("get_parameter: OMX_IndexParamPriorityMgmt\n");
            priorityMgmtType->nSize = sizeof(priorityMgmtType);
            priorityMgmtType->nVersion.nVersion = OMX_SPEC_VERSION;
            priorityMgmtType->nGroupID = m_priority_mgm.nGroupID;
            priorityMgmtType->nGroupPriority = m_priority_mgm.nGroupPriority;
            break;
        }
    case OMX_IndexParamImageInit:
        {
            OMX_PORT_PARAM_TYPE *portParamType =
            (OMX_PORT_PARAM_TYPE *)paramData;
            DEBUG_PRINT("get_parameter: OMX_IndexParamImageInit\n");
            portParamType->nVersion.nVersion = OMX_SPEC_VERSION;
            portParamType->nSize = sizeof(portParamType);
            portParamType->nPorts           = 0;
            portParamType->nStartPortNumber = 0;
            break;
        }
    case OMX_IndexParamCompBufferSupplier:
        {
            DEBUG_PRINT("get_parameter: OMX_IndexParamCompBufferSupplier\n");
            OMX_PARAM_BUFFERSUPPLIERTYPE *bufferSupplierType
            = (OMX_PARAM_BUFFERSUPPLIERTYPE*) paramData;
            DEBUG_PRINT("get_parameter: OMX_IndexParamCompBufferSupplier\n");

            bufferSupplierType->nSize = sizeof(bufferSupplierType);
            bufferSupplierType->nVersion.nVersion = OMX_SPEC_VERSION;
            if(OMX_CORE_INPUT_PORT_INDEX   == bufferSupplierType->nPortIndex)
            {
                bufferSupplierType->nPortIndex = OMX_BufferSupplyUnspecified;
            }
            else if (OMX_CORE_OUTPUT_PORT_INDEX ==
                bufferSupplierType->nPortIndex)
            {
                bufferSupplierType->nPortIndex = OMX_BufferSupplyUnspecified;
            }
            else
            {
                eRet = OMX_ErrorBadPortIndex;
            }
            DEBUG_PRINT("get_parameter:OMX_IndexParamCompBufferSupplier \
                        eRet %08x\n", eRet);
            break;
        }

        /*Component should support this port definition*/
    case OMX_IndexParamOtherInit:
        {
            OMX_PORT_PARAM_TYPE *portParamType =
            (OMX_PORT_PARAM_TYPE *)paramData;
            DEBUG_PRINT("get_parameter: OMX_IndexParamOtherInit %08x\n",
                paramIndex);
            portParamType->nVersion.nVersion = OMX_SPEC_VERSION;
            portParamType->nSize = sizeof(portParamType);
            portParamType->nPorts           = 0;
            portParamType->nStartPortNumber = 0;
            break;
        }
    default:
        {
            DEBUG_PRINT_ERROR("unknown param %08x\n", paramIndex);
            eRet = OMX_ErrorUnsupportedIndex;
        }
    }

    return eRet;

}

/**
@brief member function that set paramter from IL client

@param hComp handle to component instance
@param paramIndex parameter type
@param paramData pointer to memory space which holds the paramter
@return error status
*/
OMX_ERRORTYPE  COmxAacDec::set_parameter(OMX_IN OMX_HANDLETYPE hComp,
                                         OMX_IN OMX_INDEXTYPE paramIndex,
                                         OMX_IN OMX_PTR        paramData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Set Param in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    if(paramData == NULL)
    {
        DEBUG_PRINT("param data is NULL");
        return OMX_ErrorBadParameter;
    }
    switch(paramIndex)
    {
    case OMX_IndexParamAudioAac:
        {
            DEBUG_PRINT("SET-PARAM::OMX_IndexParamAudioAAC");
            memcpy(&m_adec_param,paramData,
                                      sizeof(OMX_AUDIO_PARAM_AACPROFILETYPE));
            DEBUG_PRINT("SF=%u Ch=%u bitrate=%u format=%d\n",
                        (unsigned int)m_adec_param.nSampleRate,
                        (unsigned int)m_adec_param.nChannels,
                        (unsigned int)m_adec_param.nBitRate,
                        m_adec_param.eAACStreamFormat);
//          set_sf(m_adec_param.nSampleRate);
//          set_ch(m_adec_param.nChannels);
//          m_pIn->set_sf(m_adec_param.nSampleRate);
//          m_pIn->set_ch(m_adec_param.nChannels);
            if(m_adec_param.eAACStreamFormat == OMX_AUDIO_AACStreamFormatADIF)
            {
                adif = true;
            }
            break;
        }
    case OMX_IndexParamPortDefinition:
        {
            OMX_PARAM_PORTDEFINITIONTYPE *portDefn;
            portDefn = (OMX_PARAM_PORTDEFINITIONTYPE *) paramData;

            if(((m_state == OMX_StateLoaded)&&
                !BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
                || (m_state == OMX_StateWaitForResources &&
                ((OMX_DirInput == portDefn->eDir && m_inp_bEnabled == true)||
                (OMX_DirInput == portDefn->eDir && m_out_bEnabled == true)))
                ||(((OMX_DirInput == portDefn->eDir && m_inp_bEnabled == false)
                ||(OMX_DirInput == portDefn->eDir && m_out_bEnabled == false))
                && (m_state != OMX_StateWaitForResources)))
            {
                DEBUG_PRINT("Set Parameter called in valid state\n");
            }
            else
            {
                DEBUG_PRINT_ERROR("Set Parameter called in Invalid State\n");
                return OMX_ErrorIncorrectStateOperation;
            }
            DEBUG_PRINT("OMX_IndexParamPortDefinition portDefn->nPortIndex =\
                %u\n", (unsigned) (portDefn->nPortIndex));
            if (OMX_CORE_INPUT_PORT_INDEX == portDefn->nPortIndex)
            {
                DEBUG_PRINT("SET_PARAMETER:OMX_IndexParamPortDefinition \
                port[%u] bufsize[%u] buf_cnt[%u]\n",
                (unsigned)(portDefn->nPortIndex),
                (unsigned)( portDefn->nBufferSize),
                (unsigned)( portDefn->nBufferCountActual));

                if(portDefn->nBufferCountActual > OMX_CORE_NUM_INPUT_BUFFERS)
                {
                    m_inp_act_buf_count = portDefn->nBufferCountActual;
                }
                else
                {
                    m_inp_act_buf_count = OMX_CORE_NUM_INPUT_BUFFERS;
                }

                if(portDefn->nBufferSize > input_buffer_size)
                {
                    input_buffer_size = portDefn->nBufferSize;
                }
                else
                {
                    input_buffer_size = OMX_CORE_INPUT_BUFFER_SIZE;
                }

            }
            else if (OMX_CORE_OUTPUT_PORT_INDEX == portDefn->nPortIndex)
            {
                DEBUG_PRINT("SET_PARAMETER:OMX_IndexParamPortDefinition \
                port[%u] bufsize[%u] buf_cnt[%u]\n",
                (unsigned)(portDefn->nPortIndex),
                (unsigned)(portDefn->nBufferSize),
                (unsigned)(portDefn->nBufferCountActual));

                if(portDefn->nBufferCountActual > OMX_CORE_NUM_OUTPUT_BUFFERS)
                {
                    m_out_act_buf_count = portDefn->nBufferCountActual;
                }
                else
                {
                    m_out_act_buf_count = OMX_CORE_NUM_OUTPUT_BUFFERS;
                }

                if(portDefn->nBufferSize > output_buffer_size)
                {
                    output_buffer_size  = portDefn->nBufferSize;
                }
                else
                {
                    output_buffer_size = OMX_AAC_OUTPUT_BUFFER_SIZE;
                }
            }
            else
            {
                DEBUG_PRINT_ERROR(" set_parameter: Bad Port idx %d",
                (int)portDefn->nPortIndex);

                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }
    case OMX_IndexParamPriorityMgmt:
        {

            DEBUG_PRINT("set_parameter: OMX_IndexParamPriorityMgmt\n");
            if(m_state != OMX_StateLoaded)
            {
                DEBUG_PRINT_ERROR("Set Parameter called in Invalid State\n");
                return OMX_ErrorIncorrectStateOperation;
            }
            OMX_PRIORITYMGMTTYPE *priorityMgmtype
            = (OMX_PRIORITYMGMTTYPE*) paramData;
            DEBUG_PRINT("set_parameter: OMX_IndexParamPriorityMgmt %u\n",
            (unsigned)( priorityMgmtype->nGroupID));
            DEBUG_PRINT("set_parameter: priorityMgmtype %u\n",
            (unsigned) (priorityMgmtype->nGroupPriority));
            m_priority_mgm.nGroupID = priorityMgmtype->nGroupID;
            m_priority_mgm.nGroupPriority = priorityMgmtype->nGroupPriority;
            break;
        }
    case  OMX_IndexParamAudioPortFormat:
        {
            OMX_AUDIO_PARAM_PORTFORMATTYPE *portFormatType =
            (OMX_AUDIO_PARAM_PORTFORMATTYPE *) paramData;
            DEBUG_PRINT("set_parameter: OMX_IndexParamAudioPortFormat\n");

            if (OMX_CORE_INPUT_PORT_INDEX== portFormatType->nPortIndex)
            {
                portFormatType->eEncoding = OMX_AUDIO_CodingAAC;
            }
            else if(OMX_CORE_OUTPUT_PORT_INDEX == portFormatType->nPortIndex)
            {
                DEBUG_PRINT("set_parameter: OMX_IndexParamAudioFormat: %u\n",
                (unsigned)(portFormatType->nIndex));
                portFormatType->eEncoding = OMX_AUDIO_CodingPCM;
            }
            else
            {
                DEBUG_PRINT_ERROR("set_parameter: Bad port index %d\n",
                (int)portFormatType->nPortIndex);
                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }


    case OMX_IndexParamCompBufferSupplier:
        {
            DEBUG_PRINT("set_parameter: OMX_IndexParamCompBufferSupplier\n");
            OMX_PARAM_BUFFERSUPPLIERTYPE *bufferSupplierType
            = (OMX_PARAM_BUFFERSUPPLIERTYPE*) paramData;
            DEBUG_PRINT("set_parameter: OMX_IndexParamCompBufferSupplier %d\n",
            bufferSupplierType->eBufferSupplier);

            if(bufferSupplierType->nPortIndex == OMX_CORE_INPUT_PORT_INDEX
                    || bufferSupplierType->nPortIndex ==
            OMX_CORE_OUTPUT_PORT_INDEX)
            {
                DEBUG_PRINT("set_parameter: \
                    OMX_IndexParamCompBufferSupplier In/Output\n");
                m_buffer_supplier.eBufferSupplier =
                    bufferSupplierType->eBufferSupplier;
            }
            else
            {
                eRet = OMX_ErrorBadPortIndex;
            }

            DEBUG_PRINT("set_parameter:OMX_IndexParamCompBufferSupplier: \
                        eRet  %08x\n", eRet);
            break;
        }

    case OMX_IndexParamAudioPcm:
        {
            DEBUG_PRINT("set_parameter: OMX_IndexParamAudioPcm\n");
            OMX_AUDIO_PARAM_PCMMODETYPE *pcmparam
            = (OMX_AUDIO_PARAM_PCMMODETYPE *) paramData;

            if (OMX_CORE_OUTPUT_PORT_INDEX== pcmparam->nPortIndex)
            {
                m_pcm_param.nChannels =  pcmparam->nChannels;
                m_pcm_param.eNumData = pcmparam->eNumData;
                m_pcm_param.bInterleaved = pcmparam->bInterleaved;
                m_pcm_param.nBitPerSample =   pcmparam->nBitPerSample;
                m_pcm_param.nSamplingRate =   pcmparam->nSamplingRate;
                m_pcm_param.ePCMMode =  pcmparam->ePCMMode;
                m_pcm_param.eChannelMapping[0] =  pcmparam->eChannelMapping[0];
                m_pcm_param.eChannelMapping[1] =  pcmparam->eChannelMapping[1];
                DEBUG_PRINT("set_parameter: Sampling rate %u",
                (unsigned)( pcmparam->nSamplingRate));
                DEBUG_PRINT("set_parameter: Number of channels %d",
                (unsigned) (pcmparam->nChannels));
            }
            else
            {
                DEBUG_PRINT_ERROR("get_parameter:OMX_IndexParamAudioPcm \
                                OMX_ErrorBadPortIndex %d\n",
                (int)pcmparam->nPortIndex);
                eRet = OMX_ErrorBadPortIndex;
            }
            break;
        }
    case OMX_IndexParamSuspensionPolicy:
        {
            OMX_PARAM_SUSPENSIONPOLICYTYPE *suspend_policy;
            suspend_policy = (OMX_PARAM_SUSPENSIONPOLICYTYPE*)paramData;
            suspensionPolicy= suspend_policy->ePolicy;
            DEBUG_PRINT("SET_PARAMETER: Set SUSPENSION POLICY %d  \
                m_ipc_to_event_th=%p\n", suspensionPolicy,
                m_ipc_to_event_th);
            break;
        }
    case OMX_IndexParamStandardComponentRole:
        {
            OMX_PARAM_COMPONENTROLETYPE *componentRole;
            componentRole = (OMX_PARAM_COMPONENTROLETYPE*)paramData;
            component_Role.nSize = componentRole->nSize;
            component_Role.nVersion = componentRole->nVersion;
            strlcpy((char *)component_Role.cRole,
                (const char*)componentRole->cRole, sizeof(component_Role.cRole));
            DEBUG_PRINT("SET_PARAMETER: role = %s\n",
                component_Role.cRole);
            break;
        }

    default:
        {
            DEBUG_PRINT_ERROR("unknown param %d\n", paramIndex);
            eRet = OMX_ErrorUnsupportedIndex;
        }
    }
    return eRet;
}

/* ======================================================================
FUNCTION
COmxAacDec::GetConfig

DESCRIPTION
OMX Get Config Method implementation.

PARAMETERS
<TBD>.

RETURN VALUE
OMX Error None if successful.

========================================================================== */
OMX_ERRORTYPE  COmxAacDec::get_config(OMX_IN OMX_HANDLETYPE      hComp,
                                      OMX_IN OMX_INDEXTYPE configIndex,
                                      OMX_INOUT OMX_PTR     configData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Get Config in Invalid State\n");
        return OMX_ErrorInvalidState;
    }

    DEBUG_PRINT("%s[%p]\n",__FUNCTION__,this);
    switch(configIndex)
    {
    case QOMX_IndexConfigAudioDualMono:
    {
        memcpy(configData, &m_adec_config_dualmono,
               sizeof(QOMX_AUDIO_CONFIG_DUALMONOTYPE));
        DEBUG_PRINT("QOMX_IndexConfigAudioDualMono: size, version,"
                    " portindex, channelconfig = %d %d %d %d\n",
                    m_adec_config_dualmono.nSize, m_adec_config_dualmono.nVersion,
                    m_adec_config_dualmono.nPortIndex, m_adec_config_dualmono.eChannelConfig);
        break;
    }
//  case OMX_IndexConfigAudioVolume:
//      {
//          OMX_AUDIO_CONFIG_VOLUMETYPE *volume =
//          (OMX_AUDIO_CONFIG_VOLUMETYPE*) configData;
//
//          if (volume->nPortIndex == OMX_CORE_INPUT_PORT_INDEX)
//          {
//              volume->nSize = sizeof(volume);
//              volume->nVersion.nVersion = OMX_SPEC_VERSION;
//              volume->bLinear = OMX_TRUE;
//              volume->sVolume.nValue = m_volume;
//              volume->sVolume.nMax   = OMX_ADEC_MAX;
//              volume->sVolume.nMin   = OMX_ADEC_MIN;
//          } else {
//              eRet = OMX_ErrorBadPortIndex;
//          }
//      }
//      break;
//
//  case OMX_IndexConfigAudioMute:
//      {
//          OMX_AUDIO_CONFIG_MUTETYPE *mute =
//          (OMX_AUDIO_CONFIG_MUTETYPE*) configData;
//
//          if (mute->nPortIndex == OMX_CORE_INPUT_PORT_INDEX)
//          {
//              mute->nSize = sizeof(mute);
//              mute->nVersion.nVersion = OMX_SPEC_VERSION;
//              mute->bMute = (BITMASK_PRESENT(&m_flags,
//              OMX_COMPONENT_MUTED)?OMX_TRUE:OMX_FALSE);
//          } else {
//              eRet = OMX_ErrorBadPortIndex;
//          }
//      }
//      break;

    default:
        eRet = OMX_ErrorUnsupportedIndex;
        break;
    }
    return eRet;
}

/* ======================================================================
FUNCTION
COmxAacDec::SetConfig

DESCRIPTION
OMX Set Config method implementation

PARAMETERS
<TBD>.

RETURN VALUE
OMX Error None if successful.
========================================================================== */
OMX_ERRORTYPE  COmxAacDec::set_config(OMX_IN OMX_HANDLETYPE      hComp,
                                      OMX_IN OMX_INDEXTYPE configIndex,
                                      OMX_IN OMX_PTR        configData)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Set Config in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    switch(configIndex)
    {
    case QOMX_IndexConfigAudioDualMono:
    {
        QOMX_AUDIO_CONFIG_DUALMONOTYPE *dualmono =
                                (QOMX_AUDIO_CONFIG_DUALMONOTYPE *)configData;
        if( m_state == OMX_StateExecuting)
        {
            struct msm_audio_aac_config aac_config;
            memset(&aac_config, 0, sizeof(aac_config));
            aac_config.dual_mono_mode = dualmono->eChannelConfig;
            if ((dualmono->eChannelConfig < OMX_AUDIO_DUAL_MONO_MODE_FL_FR) ||
                (dualmono->eChannelConfig > OMX_AUDIO_DUAL_MONO_MODE_FL_SR)){
                DEBUG_PRINT_ERROR("OMX_IndexConfigAudioDualMono: Unsupported"
                                  " DualMono Mode = %d", dualmono->eChannelConfig);\
                eRet = OMX_ErrorBadParameter;
            } else {
                DEBUG_PRINT("QOMX_IndexConfigAudioDualMono: In Executing"
                            " state, dualmono_mode = %d\n", aac_config.dual_mono_mode);
                if (ioctl(m_drv_fd, AUDIO_SET_AAC_CONFIG, &aac_config)) {
                    DEBUG_PRINT_ERROR("set_aac_config():AUDIO_SET_AAC_CONFIG failed");
                    m_first_aac_header=0;
                    post_command((unsigned)OMX_CommandStateSet,
                                 (unsigned)OMX_StateInvalid,
                                 OMX_COMPONENT_GENERATE_COMMAND);
                    return OMX_ErrorInvalidComponent;
                }
                else
                    DEBUG_PRINT("COmxAacDec: set aac config success\n");
            }
        } else {
            memcpy(&m_adec_config_dualmono,configData,
                    sizeof(QOMX_AUDIO_CONFIG_DUALMONOTYPE));
            DEBUG_PRINT("QOMX_IndexConfigAudioDualMono-SetConfig:size, version,"
                        "portindex, channelconfig = %d %d %d %d\n",
                        m_adec_config_dualmono.nSize, m_adec_config_dualmono.nVersion,
                        m_adec_config_dualmono.nPortIndex, m_adec_config_dualmono.eChannelConfig);
        }
        break;
    }
    case QOMX_IndexParamAudioAacSelectMixCoef:
    {
        OMX_U32 *mix_coeff = (OMX_U32 *)configData;
        DEBUG_PRINT("mix_coeff = %u", *mix_coeff);
        if (*mix_coeff == 0 || *mix_coeff == 1) {
            if (ioctl(m_drv_fd, AUDIO_SET_AAC_MIX_CONFIG, mix_coeff) == -1) {
                DEBUG_PRINT_ERROR("AUDIO_SET_AAC_MIX_CONFIG failed with error %d", errno);
                return OMX_ErrorUndefined;
            }
        } else {
            DEBUG_PRINT_ERROR("set_confg(), aac stereo mix coeff should be 0 or 1");
            return OMX_ErrorBadParameter;
        }
        break;
    }
//  case OMX_IndexConfigAudioVolume:
//      {
//          OMX_AUDIO_CONFIG_VOLUMETYPE *vol = (OMX_AUDIO_CONFIG_VOLUMETYPE*)
//          configData;
//          if (vol->nPortIndex == OMX_CORE_INPUT_PORT_INDEX)
//          {
//              if ((vol->sVolume.nValue <= OMX_ADEC_MAX) &&
//                      (vol->sVolume.nValue >= OMX_ADEC_MIN)) {
//                  m_volume = vol->sVolume.nValue;
//                  if (BITMASK_ABSENT(&m_flags, OMX_COMPONENT_MUTED))
//                  {
//                      /* ioctl(m_drv_fd, AUDIO_VOLUME,
//           * m_volume * OMX_ADEC_VOLUME_STEP); */
//                  }
//
//              } else {
//                  eRet = OMX_ErrorBadParameter;
//              }
//          } else {
//              eRet = OMX_ErrorBadPortIndex;
//          }
//      }
//      break;
//
//  case OMX_IndexConfigAudioMute:
//      {
//          OMX_AUDIO_CONFIG_MUTETYPE *mute = (OMX_AUDIO_CONFIG_MUTETYPE*)
//          configData;
//          if (mute->nPortIndex == OMX_CORE_INPUT_PORT_INDEX)
//          {
//              if (mute->bMute == OMX_TRUE) {
//                  BITMASK_SET(&m_flags, OMX_COMPONENT_MUTED);
//                  /* ioctl(m_drv_fd, AUDIO_VOLUME, 0); */
//              } else {
//                  BITMASK_CLEAR(&m_flags, OMX_COMPONENT_MUTED);
//                  /* ioctl(m_drv_fd, AUDIO_VOLUME,
//           * m_volume * OMX_ADEC_VOLUME_STEP); */
//              }
//          } else {
//              eRet = OMX_ErrorBadPortIndex;
//          }
//      }
//      break;

    default:
        eRet = OMX_ErrorUnsupportedIndex;
        break;
    }
    return eRet;
}

/*=============================================================================
FUNCTION:
get_extension_index

DESCRIPTION:
OMX GetExtensionIndex method implementaion.

INPUT/OUTPUT PARAMETERS:
[OUT] indexType
[IN] hComp
[IN] paramName

RETURN VALUE:
OMX_ERRORTYPE

Dependency:
None

SIDE EFFECTS:
None
=============================================================================*/
OMX_ERRORTYPE  COmxAacDec::get_extension_index(
    OMX_IN OMX_HANDLETYPE      hComp,
    OMX_IN OMX_STRING      paramName,
    OMX_OUT OMX_INDEXTYPE* indexType)
{
    if((hComp == NULL) || (paramName == NULL) || (indexType == NULL))
    {
        DEBUG_PRINT_ERROR("Returning OMX_ErrorBadParameter\n");
        return OMX_ErrorBadParameter;
    }
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Get Extension Index in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    if(strncmp(paramName,"OMX.Qualcomm.index.audio.sessionId",
        strlen("OMX.Qualcomm.index.audio.session Id")) == 0){
          *indexType =(OMX_INDEXTYPE)QOMX_IndexParamAudioSessionId;
          DEBUG_PRINT("Extension index type - %d\n", *indexType);

    }
    else if(strncmp(paramName,"OMX.Qualcomm.index.audio.dualmono",
             strlen("OMX.Qualcomm.index.audio.dualmono")) == 0)
    {
        *indexType =(OMX_INDEXTYPE)QOMX_IndexConfigAudioDualMono;
        DEBUG_PRINT("Extension index type(DualMono) - 0x%x\n", *indexType);
    }
    else if(strncmp(paramName,"OMX.Qualcomm.index.audio.aac_sel_mix_coef",
             strlen("OMX.Qualcomm.index.audio.aac_sel_mix_coef")) == 0)
    {
        *indexType =(OMX_INDEXTYPE)QOMX_IndexParamAudioAacSelectMixCoef;
        DEBUG_PRINT("Extension index type(mixCoef) - 0x%x\n", *indexType);
    }
    else
    {
          return OMX_ErrorBadParameter;
    }
    return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
COmxAacDec::GetState

DESCRIPTION
Returns the state information back to the caller.<TBD>

PARAMETERS
<TBD>.

RETURN VALUE
Error None if everything is successful.
========================================================================== */
OMX_ERRORTYPE  COmxAacDec::get_state(OMX_IN OMX_HANDLETYPE  hComp,
                                     OMX_OUT OMX_STATETYPE* state)
{
    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    *state = m_state;
    //DEBUG_PRINT("Returning the state %d\n",*state);
    return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
COmxAacDec::ComponentTunnelRequest

DESCRIPTION
OMX Component Tunnel Request method implementation. <TBD>

PARAMETERS
None.

RETURN VALUE
OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  COmxAacDec::component_tunnel_request(OMX_IN OMX_HANDLETYPE hComp,
                                     OMX_IN OMX_U32 port,
                                     OMX_IN OMX_HANDLETYPE peerComponent,
                                     OMX_IN OMX_U32        peerPort,
                                     OMX_INOUT OMX_TUNNELSETUPTYPE* tunnelSetup)
{
    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    (void)port;
    (void)peerComponent;
    (void)peerPort;
    (void)tunnelSetup;
    DEBUG_PRINT_ERROR("Error: component_tunnel_request Not Implemented\n");
    return OMX_ErrorNotImplemented;
}

void* COmxAacDec::alloc_ion_buffer(unsigned int bufsize)
{
    struct mmap_info *ion_data = NULL;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    ion_data = (struct mmap_info*) calloc(sizeof(struct mmap_info), 1);

    if(!ion_data)
    {
        DEBUG_PRINT_ERROR("\n alloc_ion_buffer: ion_data allocation failed\n");
        return NULL;
    }

    /* Align the size wrt the page boundary size of 4k */
    ion_data->map_buf_size = (bufsize + 4095) & (~4095);
    ion_data->ion_alloc_data.len = ion_data->map_buf_size;
    ion_data->ion_alloc_data.align = ION_ALLOC_ALIGN;
#ifdef QCOM_AUDIO_USE_SYSTEM_HEAP_ID
    ion_data->ion_alloc_data.heap_mask = ION_HEAP(ION_SYSTEM_HEAP_ID);
#else
    ion_data->ion_alloc_data.heap_mask = ION_HEAP(ION_AUDIO_HEAP_ID);
#endif
    ion_data->ion_alloc_data.flags = 0;
    int rc = ioctl(ionfd, ION_IOC_ALLOC, &ion_data->ion_alloc_data);
    if (rc < 0)
    {
        DEBUG_PRINT_ERROR("ION_IOC_ALLOC ioctl failed\n");
        free(ion_data);
        ion_data = NULL;
        return ion_data;
    }
    ion_data->ion_fd_data.handle = ion_data->ion_alloc_data.handle;
    rc = ioctl(ionfd, ION_IOC_SHARE, &ion_data->ion_fd_data);
    if (rc < 0)
    {
        DEBUG_PRINT_ERROR("ION_IOC SHARE ioctl failed\n");
        rc = ioctl(ionfd, ION_IOC_FREE, &(ion_data->ion_alloc_data.handle));
        if (rc < 0)
        {
            DEBUG_PRINT_ERROR("ION_IOC_FREE ioctl failed\n");
        }
        free(ion_data);
        ion_data = NULL;
        return ion_data;
    }
    /* Map the ION shared file descriptor into current process address space */
    ion_data->pBuffer = (OMX_U8*) mmap(NULL,
                                    ion_data->map_buf_size,
                                    PROT_READ | PROT_WRITE,
                                    MAP_SHARED,
                                    ion_data->ion_fd_data.fd,
                                    0);
    if(MAP_FAILED == ion_data->pBuffer)
    {
        DEBUG_PRINT_ERROR("mmap() failed \n");
        close(ion_data->ion_fd_data.fd);
        rc = ioctl(ionfd, ION_IOC_FREE, &(ion_data->ion_alloc_data.handle));
        if (rc < 0)
        {
            DEBUG_PRINT_ERROR("ION_IOC_FREE failed\n");
        }
        free(ion_data);
        ion_data = NULL;
    }
    return ion_data;
}

void COmxAacDec::free_ion_buffer(void** ion_buffer)
{
    struct mmap_info** ion_data = (struct mmap_info**) ion_buffer;
    if(ion_data && (*ion_data))
    {
        if ((*ion_data)->pBuffer &&
                (EINVAL == munmap ((*ion_data)->pBuffer,
                   (*ion_data)->map_buf_size))) {
            DEBUG_PRINT_ERROR("\n Error in Unmapping the buffer %p\n",
            (*ion_data)->pBuffer);
        }
        (*ion_data)->pBuffer = NULL;

        close((*ion_data)->ion_fd_data.fd);
        int rc = ioctl(ionfd, ION_IOC_FREE, &((*ion_data)->ion_alloc_data.handle));
        if (rc < 0)
        {
            DEBUG_PRINT_ERROR("ION_IOC_FREE failed\n");
        }
        free(*ion_data);
        *ion_data = NULL;
    }
    else
    {
        DEBUG_PRINT ("\n free_ion_buffer: Invalid input parameter\n");
    }
}

/* ======================================================================
FUNCTION
COmxAacDec::AllocateInputBuffer

DESCRIPTION
Helper function for allocate buffer in the input pin

PARAMETERS
None.

RETURN VALUE
true/false

========================================================================== */
OMX_ERRORTYPE  COmxAacDec::allocate_input_buffer(OMX_IN OMX_HANDLETYPE hComp,
                                  OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                                  OMX_IN OMX_U32                        port,
                                  OMX_IN OMX_PTR                     appData,
                                  OMX_IN OMX_U32                       bytes)
{
    char *buf_ptr;
    unsigned nBufSize = bytes;
    unsigned int nMapBufSize = 0;
    struct mmap_info *mmap_data = NULL;
    OMX_BUFFERHEADERTYPE *bufHdr;
    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    (void)port;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    DEBUG_PRINT("Inside COmxAacDec::allocate_input_buffer");
    if(bytes < input_buffer_size)
    {
        /* return if i\p buffer size provided by client is less
        than min i\p buffer size supported by omx component */
        DEBUG_PRINT("\nError: bytes[%u] < input_buffer_size[%u]\n",
            (unsigned) bytes, input_buffer_size);
        return OMX_ErrorInsufficientResources;
    }

    if(m_inp_current_buf_count < m_inp_act_buf_count)
    {
        buf_ptr = (char *) calloc( sizeof(OMX_BUFFERHEADERTYPE) , 1);

        if (buf_ptr != NULL)
        {
            bufHdr = (OMX_BUFFERHEADERTYPE *) buf_ptr;

            if(pcm_feedback)
            {
                /* Add META_IN size */
                nMapBufSize = nBufSize + sizeof(META_IN);
            }
            else
            {
                nMapBufSize = nBufSize;
            }

            mmap_data =(struct mmap_info *)alloc_ion_buffer(nMapBufSize);
            if(mmap_data== NULL)
            {
                DEBUG_PRINT_ERROR("%s[%p]ion allocation failed",
                __FUNCTION__,this);
                free(buf_ptr);
                return OMX_ErrorInsufficientResources;
            }

            if(pcm_feedback)
            {
                bufHdr->pBuffer = (OMX_U8*)mmap_data->pBuffer+ sizeof(META_IN);
            }
            else
            {
                bufHdr->pBuffer = (OMX_U8*)mmap_data->pBuffer;
            }
            audio_register_ion(mmap_data);

            *bufferHdr = bufHdr;

            DEBUG_PRINT("Allocate_input:bufHdr %x bufHdr->pBuffer %x",
                (unsigned) bufHdr, (unsigned)  bufHdr->pBuffer);

            bufHdr->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
            bufHdr->nVersion.nVersion = OMX_SPEC_VERSION;
            bufHdr->nAllocLen         = nBufSize;
            bufHdr->pAppPrivate       = appData;
            bufHdr->nInputPortIndex   = OMX_CORE_INPUT_PORT_INDEX;
            bufHdr->nOffset           = 0;
            m_input_buf_hdrs.insert(bufHdr, (OMX_BUFFERHEADERTYPE*)mmap_data);
            m_inp_current_buf_count++;
        }
        else
        {
            DEBUG_PRINT("Allocate_input:I/P buffer memory allocation failed\n");
            eRet = OMX_ErrorInsufficientResources;
        }
    }
    else
    {
        DEBUG_PRINT("\nCould not allocate more buffers than ActualBufCnt\n");
        eRet = OMX_ErrorInsufficientResources;
    }

    return eRet;
}

OMX_ERRORTYPE  COmxAacDec::allocate_output_buffer(OMX_IN OMX_HANDLETYPE hComp,
                                    OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                                    OMX_IN OMX_U32                        port,
                                    OMX_IN OMX_PTR                     appData,
                                    OMX_IN OMX_U32                       bytes)
{
    char *buf_ptr;
    int ion_fd = -1;
    unsigned nBufSize = bytes;
    unsigned int nMapBufSize = 0;
    struct mmap_info *mmap_data = NULL;
    OMX_BUFFERHEADERTYPE *bufHdr;
    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    (void)port;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    DEBUG_PRINT("Inside COmxAacDec::allocate_output_buffer");
    if(bytes < output_buffer_size)
    {
        /* return if o\p buffer size provided by client is less
        than min o\p buffer size supported by omx component */
        DEBUG_PRINT("\nError: bytes[%u] < output_buffer_size[%u]\n",
            (unsigned)bytes, output_buffer_size);
        return OMX_ErrorInsufficientResources;
    }

    if(m_out_current_buf_count < m_out_act_buf_count)
    {
        buf_ptr = (char *) calloc( sizeof(OMX_BUFFERHEADERTYPE) , 1);

        if (buf_ptr != NULL)
        {
            bufHdr = (OMX_BUFFERHEADERTYPE *) buf_ptr;

        /* Allocate buffer including META_OUT */
            nMapBufSize = nBufSize + sizeof(META_OUT);

            mmap_data =(struct mmap_info *)alloc_ion_buffer(nMapBufSize);

            if(mmap_data== NULL)
            {
                DEBUG_PRINT_ERROR("%s[%p]ion allocation failed",
                __FUNCTION__,this);
                free(buf_ptr);
                return OMX_ErrorInsufficientResources;
            }
        /* Point to after META_OUT */
            bufHdr->pBuffer = (OMX_U8*)mmap_data->pBuffer + sizeof(META_OUT);

            audio_register_ion(mmap_data);

            *bufferHdr = bufHdr;

            DEBUG_PRINT("Allocate_output:bufHdr %x bufHdr->pBuffer %x",
                (unsigned) bufHdr, (unsigned)( bufHdr->pBuffer));

            bufHdr->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
            bufHdr->nVersion.nVersion = OMX_SPEC_VERSION;
            bufHdr->nAllocLen         = nBufSize;
            bufHdr->pAppPrivate       = appData;
            bufHdr->nOutputPortIndex  = OMX_CORE_OUTPUT_PORT_INDEX;
            bufHdr->nOffset           = 0;
            bufHdr->pOutputPortPrivate = (void*)ion_fd;
            m_output_buf_hdrs.insert(bufHdr, (OMX_BUFFERHEADERTYPE*)mmap_data);
            m_out_current_buf_count++;
        }
        else
        {
            DEBUG_PRINT("Allocate_output:O/P buffer memory allocation \
                failed\n");
            eRet = OMX_ErrorInsufficientResources;
        }
    }
    else
    {
        DEBUG_PRINT("\nCould not allocate more buffers than ActualBufCnt\n");
        eRet = OMX_ErrorInsufficientResources;
    }
    return eRet;
}


// AllocateBuffer  -- API Call
/* ======================================================================
FUNCTION
COmxAacDec::AllocateBuffer

DESCRIPTION
Returns zero if all the buffers released..

PARAMETERS
None.

RETURN VALUE
true/false

========================================================================== */
OMX_ERRORTYPE  COmxAacDec::allocate_buffer(OMX_IN OMX_HANDLETYPE hComp,
                                    OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                                    OMX_IN OMX_U32                        port,
                                    OMX_IN OMX_PTR                     appData,
                                    OMX_IN OMX_U32                       bytes)
{

    OMX_ERRORTYPE eRet = OMX_ErrorNone; // OMX return type
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if(m_state == OMX_StateInvalid)
    {
        DEBUG_PRINT_ERROR("Allocate Buf in Invalid State\n");
        return OMX_ErrorInvalidState;
    }
    if(port == OMX_CORE_INPUT_PORT_INDEX)
    {
        eRet = allocate_input_buffer(hComp,bufferHdr,port,appData,bytes);
    }
    else if(port == OMX_CORE_OUTPUT_PORT_INDEX)
    {
        eRet = allocate_output_buffer(hComp,bufferHdr,port,appData,bytes);
    }
    else
    {
        DEBUG_PRINT_ERROR("allocate_buffer:Error--> Invalid Port \
            Index received %d\n", (int)port);
        eRet = OMX_ErrorBadPortIndex;
    }

    if((eRet == OMX_ErrorNone))
    {
        DEBUG_PRINT("Checking for Output Allocate buffer Done");
        if(allocate_done())
        {
            m_is_alloc_buf++;
            if (BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
            {
                BITMASK_CLEAR(&m_flags, OMX_COMPONENT_IDLE_PENDING);
                post_command(OMX_CommandStateSet,OMX_StateIdle,
                OMX_COMPONENT_GENERATE_EVENT);
            }
        }

        if((port == OMX_CORE_INPUT_PORT_INDEX) && m_inp_bPopulated)
        {
            if(BITMASK_PRESENT(&m_flags,OMX_COMPONENT_INPUT_ENABLE_PENDING))
            {
                BITMASK_CLEAR((&m_flags),OMX_COMPONENT_INPUT_ENABLE_PENDING);
                post_command(OMX_CommandPortEnable, OMX_CORE_INPUT_PORT_INDEX,
                OMX_COMPONENT_GENERATE_EVENT);
            }
        }
        if((port == OMX_CORE_OUTPUT_PORT_INDEX) && m_out_bPopulated)
        {
            if(BITMASK_PRESENT(&m_flags,OMX_COMPONENT_OUTPUT_ENABLE_PENDING))
            {
                BITMASK_CLEAR((&m_flags),OMX_COMPONENT_OUTPUT_ENABLE_PENDING);
                m_out_bEnabled = OMX_TRUE;
                bOutputPortReEnabled = 1;
                DEBUG_PRINT("AllocBuf-->is_out_th_sleep=%d\n",is_out_th_sleep);
                if(is_out_th_sleep)
                {
                    is_out_th_sleep = false;
                    DEBUG_DETAIL("AllocBuf:WAKING UP OUT THREADS\n");
                    out_th_wakeup();
                }
                if(is_in_th_sleep)
                {
                    is_in_th_sleep = false;
                    DEBUG_DETAIL("AB:WAKING UP IN THREADS\n");
                    in_th_wakeup();
                }
                post_command(OMX_CommandPortEnable, OMX_CORE_OUTPUT_PORT_INDEX,
                OMX_COMPONENT_GENERATE_EVENT);
            }
        }
    }
    DEBUG_PRINT("Allocate Buffer exit with ret Code[%d] port[%u] \
            inp_populated %d oup_populated %d\n", eRet,(unsigned)port,
            m_inp_bPopulated, m_out_bPopulated);
    return eRet;
}

/* ======================================================================
FUNCTION
COmxAacDec::UseBuffer

DESCRIPTION
OMX Use Buffer method implementation.

PARAMETERS
<TBD>.

RETURN VALUE
OMX Error None , if everything successful.

========================================================================== */
OMX_ERRORTYPE  COmxAacDec::use_buffer(
                                    OMX_IN OMX_HANDLETYPE            hComp,
                                    OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                                    OMX_IN OMX_U32                   port,
                                    OMX_IN OMX_PTR                   appData,
                                    OMX_IN OMX_U32                   bytes,
                                    OMX_IN OMX_U8*                   buffer)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if(port == OMX_CORE_INPUT_PORT_INDEX)
    {
        eRet = use_input_buffer(hComp,bufferHdr,port,appData,bytes,buffer);
    }
    else if(port == OMX_CORE_OUTPUT_PORT_INDEX)
    {
        eRet = use_output_buffer(hComp,bufferHdr,port,appData,bytes,buffer);
    }
    else
    {
        DEBUG_PRINT_ERROR("Error: Invalid Port Index received %d\n",(int)port);
        eRet = OMX_ErrorBadPortIndex;
    }

    if((eRet == OMX_ErrorNone))
    {
        if(allocate_done())
        {
            if (BITMASK_PRESENT(&m_flags,OMX_COMPONENT_IDLE_PENDING))
            {
                BITMASK_CLEAR(&m_flags, OMX_COMPONENT_IDLE_PENDING);
                post_command(OMX_CommandStateSet,OMX_StateIdle,
                OMX_COMPONENT_GENERATE_EVENT);
            }
        }

        if((port == OMX_CORE_INPUT_PORT_INDEX) && m_inp_bPopulated)
        {
            if(BITMASK_PRESENT(&m_flags,OMX_COMPONENT_INPUT_ENABLE_PENDING))
            {
                BITMASK_CLEAR((&m_flags),OMX_COMPONENT_INPUT_ENABLE_PENDING);
                post_command(OMX_CommandPortEnable, OMX_CORE_INPUT_PORT_INDEX,
                OMX_COMPONENT_GENERATE_EVENT);
            }
        }
        if((port == OMX_CORE_OUTPUT_PORT_INDEX) && m_out_bPopulated)
        {
            if(BITMASK_PRESENT(&m_flags,OMX_COMPONENT_OUTPUT_ENABLE_PENDING))
            {
                BITMASK_CLEAR((&m_flags),OMX_COMPONENT_OUTPUT_ENABLE_PENDING);
                post_command(OMX_CommandPortEnable, OMX_CORE_OUTPUT_PORT_INDEX,
                OMX_COMPONENT_GENERATE_EVENT);
                m_out_bPopulated = OMX_TRUE;
                bOutputPortReEnabled = 1;
                if(is_out_th_sleep)
                {
                    is_out_th_sleep = false;
                    DEBUG_DETAIL("UseBuf:WAKING UP OUT THREADS\n");
                    out_th_wakeup();
                }
                if(is_in_th_sleep)
                {
                    is_in_th_sleep = false;
                    DEBUG_DETAIL("UB:WAKING UP IN THREADS\n");
                    in_th_wakeup();
                }
            }
        }
    }
    DEBUG_PRINT("Use Buffer for port[%u] eRet[%d]\n", (unsigned)port,eRet);
    return eRet;
}
/* ======================================================================
FUNCTION
COmxAacDec::UseInputBuffer

DESCRIPTION
Helper function for Use buffer in the input pin

PARAMETERS
None.

RETURN VALUE
true/false

========================================================================== */
OMX_ERRORTYPE  COmxAacDec::use_input_buffer(
                                    OMX_IN OMX_HANDLETYPE            hComp,
                                    OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                                    OMX_IN OMX_U32                   port,
                                    OMX_IN OMX_PTR                   appData,
                                    OMX_IN OMX_U32                   bytes,
                                    OMX_IN OMX_U8*                   buffer)
{
    unsigned nBufSize = bytes;
    unsigned int nMapBufSize = 0;
    struct mmap_info *mmap_data = NULL;
    char *buf_ptr = NULL, *loc_buf_ptr = NULL;
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE *bufHdr = NULL, *loc_bufHdr = NULL;

    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    (void)port;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    DEBUG_PRINT("Inside COmxAacDec::use_input_buffer");
    if(bytes < input_buffer_size)
    {
        /* return if i\p buffer size provided by client is less
        than min i\p buffer size supported by omx component */
        DEBUG_PRINT("\nError: bytes[%u] < input_buffer_size[%u]\n",
            (unsigned) bytes, input_buffer_size);
        return OMX_ErrorInsufficientResources;
    }

    if(m_inp_current_buf_count < m_inp_act_buf_count)
    {
        buf_ptr = (char *) calloc( sizeof(OMX_BUFFERHEADERTYPE) , 1);
        loc_buf_ptr = (char *) calloc( sizeof(OMX_BUFFERHEADERTYPE) , 1);

        if (buf_ptr != NULL && loc_buf_ptr != NULL)
        {
            bufHdr = (OMX_BUFFERHEADERTYPE *) buf_ptr;
            loc_bufHdr = (OMX_BUFFERHEADERTYPE *) loc_buf_ptr;

            if(pcm_feedback)
            {
                /* Add META_IN size */
                nMapBufSize = nBufSize + sizeof(META_IN);
            }
            else
            {
                nMapBufSize = nBufSize;
            }
            mmap_data =(struct mmap_info *)alloc_ion_buffer(nMapBufSize);

            if(mmap_data== NULL)
            {
                DEBUG_PRINT_ERROR("%s[%p]ion allocation failed",
                __FUNCTION__,this);
                free(buf_ptr);
                free(loc_bufHdr);
                return OMX_ErrorInsufficientResources;
            }


            /* Register the mapped ION buffer with the AAC driver */
            audio_register_ion(mmap_data);
            *bufferHdr = bufHdr;

            bufHdr->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
            bufHdr->nVersion.nVersion = OMX_SPEC_VERSION;
            bufHdr->nAllocLen         = nBufSize;
            bufHdr->pAppPrivate       = appData;
            bufHdr->nInputPortIndex   = OMX_CORE_INPUT_PORT_INDEX;
            bufHdr->nOffset           = 0;

            memcpy(loc_bufHdr, bufHdr, sizeof(OMX_BUFFERHEADERTYPE));
            bufHdr->pBuffer = (OMX_U8 *)(buffer);

            if(pcm_feedback)
            {
                loc_bufHdr->pBuffer =
               (OMX_U8*)mmap_data->pBuffer + sizeof(META_IN);
            }
            else
            {
                loc_bufHdr->pBuffer = (OMX_U8*)mmap_data->pBuffer;
            }

            DEBUG_PRINT("\nUse_input:bufHdr %x bufHdr->pBuffer %x size %d",
                (unsigned)bufHdr, (unsigned)( bufHdr->pBuffer), nBufSize);
            DEBUG_PRINT("\nUse_input:locbufHdr %x locbufHdr->pBuffer %x size %d",
            (unsigned) loc_bufHdr, (unsigned)(loc_bufHdr->pBuffer), nBufSize);

            m_input_buf_hdrs.insert(bufHdr, (OMX_BUFFERHEADERTYPE*)mmap_data);
            m_loc_in_use_buf_hdrs.insert(bufHdr, loc_bufHdr);
            m_loc_in_use_buf_hdrs.insert(loc_bufHdr, bufHdr);

            m_inp_current_buf_count++;

            if(m_inp_current_buf_count == m_inp_act_buf_count)
            {
                m_in_use_buf_case = true;
            }
        }
        else
        {
            DEBUG_PRINT("Useinput:I/P buffer header memory \
                allocation failed\n");
            if(buf_ptr)
            {
                free(buf_ptr);
                buf_ptr = NULL;
            }

            if(loc_buf_ptr)
            {
                free(loc_buf_ptr);
                loc_buf_ptr = NULL;
            }

            eRet = OMX_ErrorInsufficientResources;
        }
    }
    else
    {
        DEBUG_PRINT("\nCould not use more buffers than ActualBufCnt\n");
        eRet = OMX_ErrorInsufficientResources;
    }

    return eRet;

}

/* ======================================================================
FUNCTION
COmxAacDec::UseOutputBuffer

DESCRIPTION
Helper function for Use buffer in the input pin

PARAMETERS
None.

RETURN VALUE
true/false

========================================================================== */
OMX_ERRORTYPE  COmxAacDec::use_output_buffer(
                                    OMX_IN OMX_HANDLETYPE            hComp,
                                    OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                                    OMX_IN OMX_U32                   port,
                                    OMX_IN OMX_PTR                   appData,
                                    OMX_IN OMX_U32                   bytes,
                                    OMX_IN OMX_U8*                   buffer)
{
    unsigned nBufSize = bytes;
    unsigned int nMapBufSize = 0;
    struct mmap_info *mmap_data = NULL;
    char *buf_ptr = NULL, *loc_buf_ptr = NULL;
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    OMX_BUFFERHEADERTYPE *bufHdr = NULL, *loc_bufHdr = NULL;

    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    (void)port;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    DEBUG_PRINT("Inside COmxAacDec::use_output_buffer");
    if(bytes < output_buffer_size)
    {
        /* return if o\p buffer size provided by client is less
        than min o\p buffer size supported by omx component */
        DEBUG_PRINT("\nError: bytes[%u] < output_buffer_size[%u]\n",
            (unsigned) bytes, output_buffer_size);
        return OMX_ErrorInsufficientResources;
    }


    if(m_out_current_buf_count < m_out_act_buf_count)
    {
        buf_ptr = (char *) calloc( sizeof(OMX_BUFFERHEADERTYPE) , 1);
        loc_buf_ptr = (char *) calloc( sizeof(OMX_BUFFERHEADERTYPE) , 1);

        if (buf_ptr != NULL && loc_buf_ptr != NULL)
        {
            bufHdr = (OMX_BUFFERHEADERTYPE *) buf_ptr;
            loc_bufHdr = (OMX_BUFFERHEADERTYPE *) loc_buf_ptr;

            nMapBufSize = nBufSize + sizeof(META_OUT);
            mmap_data =(struct mmap_info *)alloc_ion_buffer(nMapBufSize);

            if(mmap_data== NULL)
            {
                DEBUG_PRINT_ERROR("%s[%p]ion allocation failed",
                __FUNCTION__,this);
                free(buf_ptr);
                free(loc_bufHdr);
                return OMX_ErrorInsufficientResources;
            }


            /* Register the mapped ION buffer with the AAC driver */
            audio_register_ion(mmap_data);

            *bufferHdr = bufHdr;

            bufHdr->nSize             = sizeof(OMX_BUFFERHEADERTYPE);
            bufHdr->nVersion.nVersion = OMX_SPEC_VERSION;
            bufHdr->nAllocLen         = nBufSize;
            bufHdr->pAppPrivate       = appData;
            bufHdr->nOutputPortIndex  = OMX_CORE_OUTPUT_PORT_INDEX;
            bufHdr->nOffset           = 0;

            memcpy(loc_bufHdr, bufHdr, sizeof(OMX_BUFFERHEADERTYPE));
            bufHdr->pBuffer = (OMX_U8 *)(buffer);
            loc_bufHdr->pBuffer =
            (OMX_U8 *)mmap_data->pBuffer + sizeof(META_OUT);

            DEBUG_PRINT("Use_Output:bufHdr %x bufHdr->pBuffer %x size %d ",
            (unsigned)bufHdr,(unsigned) bufHdr->pBuffer,nBufSize);

            m_output_buf_hdrs.insert(bufHdr, (OMX_BUFFERHEADERTYPE*)mmap_data);
            m_loc_out_use_buf_hdrs.insert(bufHdr, loc_bufHdr);
            m_loc_out_use_buf_hdrs.insert(loc_bufHdr, bufHdr);

            m_out_current_buf_count++;

            if(m_out_current_buf_count == m_out_act_buf_count)
            {
                m_out_use_buf_case = true;
            }
        }
        else
        {
            DEBUG_PRINT("Useoutput:O/P buffer hdr memory allocation failed\n");
            if(buf_ptr)
            {
                free(buf_ptr);
                buf_ptr = NULL;
            }

            if(loc_buf_ptr)
            {
                free(loc_buf_ptr);
                loc_buf_ptr = NULL;
            }

            eRet = OMX_ErrorInsufficientResources;
        }
    }
    else
    {
        DEBUG_PRINT("\nCould not use more buffers than ActualBufCnt\n");
        eRet = OMX_ErrorInsufficientResources;
    }

    return eRet;

}

/**
@brief member function that searches for caller buffer

@param buffer pointer to buffer header
@return bool value indicating whether buffer is found
*/
bool COmxAacDec::search_input_bufhdr(OMX_BUFFERHEADERTYPE *buffer)
{
    bool eRet = false;
    OMX_BUFFERHEADERTYPE *temp = NULL;

    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    //access only in IL client context
    temp = m_input_buf_hdrs.find_ele(buffer);
    if(buffer && temp)
    {
        DEBUG_DETAIL("search_input_bufhdr %x \n",(unsigned) buffer);
        eRet = true;
    }
    return eRet;
}

/**
@brief member function that searches for caller buffer

@param buffer pointer to buffer header
@return bool value indicating whether buffer is found
*/
bool COmxAacDec::search_output_bufhdr(OMX_BUFFERHEADERTYPE *buffer)
{
    bool eRet = false;
    OMX_BUFFERHEADERTYPE *temp = NULL;

    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);

    //access only in IL client context
    temp = m_output_buf_hdrs.find_ele(buffer);
    if(buffer && temp)
    {
        DEBUG_DETAIL("search_output_bufhdr %x \n", (unsigned)buffer);
        eRet = true;
    }

    return eRet;
}

// Free Buffer - API call
/**
@brief member function that handles free buffer command from IL client

This function is a block-call function that handles IL client request to
freeing the buffer

@param hComp handle to component instance
@param port id of port which holds the buffer
@param buffer buffer header
@return Error status
*/
OMX_ERRORTYPE  COmxAacDec::free_buffer(OMX_IN OMX_HANDLETYPE hComp,
                                    OMX_IN OMX_U32                 port,
                                    OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{
    OMX_BUFFERHEADERTYPE *bufHdr = buffer;
    struct mmap_info *mmap_data = NULL;
    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    DEBUG_PRINT("Free_Buffer buf %x port=%u\n",
            (unsigned)buffer,(unsigned)port);

    if(m_state == OMX_StateIdle &&
            (BITMASK_PRESENT(&m_flags ,OMX_COMPONENT_LOADING_PENDING)))
    {
        DEBUG_PRINT(" free buffer while Component in Loading pending\n");
    }
    else if((m_inp_bEnabled == OMX_FALSE && port == OMX_CORE_INPUT_PORT_INDEX)||
    (m_out_bEnabled == OMX_FALSE && port == OMX_CORE_OUTPUT_PORT_INDEX))
    {
        DEBUG_PRINT("Free Buffer while port %u disabled\n", (unsigned)port);
    }
    else if(m_state == OMX_StateExecuting || m_state == OMX_StatePause)
    {
        DEBUG_PRINT("Invalid state to free buffer,ports need to be disabled:\
                    OMX_ErrorPortUnpopulated\n");
        m_cb.EventHandler(&m_cmp,
        m_app_data,
        OMX_EventError,
        OMX_ErrorPortUnpopulated,
        NULL,
        NULL );
        return eRet;
    }
    else
    {
        DEBUG_PRINT("free_buffer: Invalid state to free buffer,ports need to be\
                    disabled:OMX_ErrorPortUnpopulated\n");
        m_cb.EventHandler(&m_cmp,
        m_app_data,
        OMX_EventError,
        OMX_ErrorPortUnpopulated,
        NULL,
        NULL );
    }

    if(port == OMX_CORE_INPUT_PORT_INDEX)
    {
        if(m_inp_current_buf_count != 0)
        {
            m_inp_bPopulated = OMX_FALSE;
            if(search_input_bufhdr(buffer) == true)
            {
                /* Buffer exist */
                //access only in IL client context
                DEBUG_PRINT("Free_Buf:in_buffer[%p]\n",buffer);
                mmap_data = (struct mmap_info*) m_input_buf_hdrs.find(buffer);

                if(m_in_use_buf_case)
                {
                    bufHdr = m_loc_in_use_buf_hdrs.find(buffer);
                }

                if(mmap_data)
                {
                    audio_deregister_ion(mmap_data);
                    if (mmap_data->pBuffer &&
                            (EINVAL == munmap (mmap_data->pBuffer,
                           mmap_data->map_buf_size)))
                    {
                        DEBUG_PRINT ("\n Error in Unmapping the buffer %p",
                    bufHdr);
                    }
                    mmap_data->pBuffer = NULL;
                    close(mmap_data->ion_fd_data.fd);
                    if(ioctl(ionfd, ION_IOC_FREE, &(mmap_data->ion_alloc_data.handle)) < 0)
                    {
                        DEBUG_PRINT ("ION_IOC_FREE failed\n");
                    }
                    free(mmap_data);
                    mmap_data = NULL;
                }

                if(m_in_use_buf_case && bufHdr)
                {
                    m_loc_in_use_buf_hdrs.erase(buffer);
                    m_loc_in_use_buf_hdrs.erase(bufHdr);
                    bufHdr->pBuffer = NULL;
                    free(bufHdr);
                    if (buffer == bufHdr)
                        buffer = NULL;
                    bufHdr = NULL;
                }

                if(buffer)
                {
                    m_input_buf_hdrs.erase(buffer);
                    free(buffer);
                    buffer = NULL;
                }

                m_inp_current_buf_count--;

                if(!m_inp_current_buf_count)
                {
                    m_in_use_buf_case = false;
                }

                DEBUG_PRINT("Free_Buf:in_buffer[%p] input buffer count = %d\n",
                buffer,
                m_inp_current_buf_count);
            }
            else
            {
                DEBUG_PRINT_ERROR("Free_Buf:Error-->free_buffer, \
                                Invalid Input buffer header\n");
                eRet = OMX_ErrorBadParameter;
            }
        }
        else
        {
            DEBUG_PRINT_ERROR("Error: free_buffer,Port Index calculation \
                            came out Invalid\n");
            eRet = OMX_ErrorBadPortIndex;
        }

        if(BITMASK_PRESENT((&m_flags),OMX_COMPONENT_INPUT_DISABLE_PENDING)
                && release_done(0))
        {
            bOutputPortReEnabled = 0;
            DEBUG_PRINT("INPUT PORT MOVING TO DISABLED STATE \n");
            BITMASK_CLEAR((&m_flags),OMX_COMPONENT_INPUT_DISABLE_PENDING);
            post_command(OMX_CommandPortDisable,
            OMX_CORE_INPUT_PORT_INDEX,
            OMX_COMPONENT_GENERATE_EVENT);
        }
    }
    else if(port == OMX_CORE_OUTPUT_PORT_INDEX)
    {
        if(m_out_current_buf_count != 0)
        {
            m_out_bPopulated = OMX_FALSE;
            if(search_output_bufhdr(buffer) == true)
            {
                /* Buffer exist */
                //access only in IL client context
                mmap_data = (struct mmap_info*) m_output_buf_hdrs.find(buffer);

                if(m_out_use_buf_case)
                {
                    bufHdr = m_loc_out_use_buf_hdrs.find(buffer);
                }

                if(mmap_data)
                {
                    audio_deregister_ion(mmap_data);
                    if (mmap_data->pBuffer &&
                            (EINVAL == munmap (mmap_data->pBuffer,
                           mmap_data->map_buf_size)))
                    {
                        DEBUG_PRINT ("\n Error in Unmapping the buffer %p",
                    bufHdr);
                    }
                    mmap_data->pBuffer = NULL;
                    close(mmap_data->ion_fd_data.fd);
                    if(ioctl(ionfd, ION_IOC_FREE, &(mmap_data->ion_alloc_data.handle)) < 0)
                    {
                        DEBUG_PRINT ("ION_IOC_FREE failed\n");
                    }
                    free(mmap_data);
                    mmap_data = NULL;
                }

                if(m_out_use_buf_case && bufHdr)
                {
                    m_loc_out_use_buf_hdrs.erase(buffer);
                    m_loc_out_use_buf_hdrs.erase(bufHdr);
                    bufHdr->pBuffer = NULL;
                    free(bufHdr);
                    if (buffer == bufHdr)
                        buffer = NULL;
                    bufHdr = NULL;
                }

                if(buffer)
                {
                    m_output_buf_hdrs.erase(buffer);
                    free(buffer);
                    buffer = NULL;
                }

                m_out_current_buf_count--;

                if(!m_out_current_buf_count)
                {
                    m_out_use_buf_case = false;
                }

                DEBUG_PRINT("Free_Buf:out_buffer[%p] output buffer \
            count = %d\n",buffer,m_out_current_buf_count);
            }
            else
            {
                DEBUG_PRINT("Free_Buf:Error-->free_buffer , \
                            Invalid Output buffer header\n");
                eRet = OMX_ErrorBadParameter;
            }
        }
        else
        {
            eRet = OMX_ErrorBadPortIndex;
        }

        if(BITMASK_PRESENT((&m_flags),OMX_COMPONENT_OUTPUT_DISABLE_PENDING)
                && release_done(1))
        {
            bOutputPortReEnabled = 0;
            DEBUG_PRINT("OUTPUT PORT MOVING TO DISABLED STATE \n");
            BITMASK_CLEAR((&m_flags),OMX_COMPONENT_OUTPUT_DISABLE_PENDING);
            post_command(OMX_CommandPortDisable,
            OMX_CORE_OUTPUT_PORT_INDEX,
            OMX_COMPONENT_GENERATE_EVENT);
        }
    }
    else
    {
        eRet = OMX_ErrorBadPortIndex;
    }

    if((eRet == OMX_ErrorNone) &&
            (BITMASK_PRESENT(&m_flags ,OMX_COMPONENT_LOADING_PENDING)))
    {
        if(release_done(OMX_ALL))
        {
            if(ioctl(m_drv_fd,AUDIO_ABORT_GET_EVENT,NULL) < 0)
            DEBUG_PRINT("AUDIO ABORT_GET_EVENT in free buffer failed\n");
            else
            DEBUG_PRINT("AUDIO ABORT_GET_EVENT in free buffer passed\n");

            if (m_ipc_to_event_th != NULL)
            {
                omx_aac_thread_stop(m_ipc_to_event_th);
                m_ipc_to_event_th = NULL;
            }
            if(ioctl(m_drv_fd, AUDIO_STOP, 0) < 0)
            DEBUG_PRINT("AUDIO STOP in free buffer failed\n");
            else
            DEBUG_PRINT("AUDIO STOP in free buffer passed\n");
            m_first_aac_header= 0;
            m_is_alloc_buf = 0;

            DEBUG_PRINT("Free_Buf: Free buffer\n");

            // Send the callback now
            BITMASK_CLEAR((&m_flags),OMX_COMPONENT_LOADING_PENDING);
            post_command(OMX_CommandStateSet,
            OMX_StateLoaded,OMX_COMPONENT_GENERATE_EVENT);
            DEBUG_PRINT("OMX_StateLoaded OMX_COMPONENT_GENERATE_EVENT\n");
        }
    }

    return eRet;
}


/**
@brief member function that that handles empty this buffer command

This function meremly queue up the command and data would be consumed
in command server thread context

@param hComp handle to component instance
@param buffer pointer to buffer header
@return error status
*/
OMX_ERRORTYPE  COmxAacDec::empty_this_buffer(OMX_IN OMX_HANDLETYPE hComp,
                                    OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{
    OMX_STATETYPE state;
    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if (buffer == NULL)
	    return OMX_ErrorBadParameter;

    pthread_mutex_lock(&m_state_lock);
    get_state(&m_cmp, &state);
    pthread_mutex_unlock(&m_state_lock);
    if ( state == OMX_StateInvalid )
    {
        DEBUG_PRINT("Empty this buffer in Invalid State\n");
        return OMX_ErrorInvalidState;
    }

    if((buffer->nFilledLen > buffer->nAllocLen))
    {
        DEBUG_PRINT("ETB: buffer->nFilledLen[%u] > buffer->nAllocLen[%u]",
        (unsigned) buffer->nFilledLen,(unsigned) buffer->nAllocLen);
        return OMX_ErrorBadParameter;
    }

    if ( (buffer->nInputPortIndex == 0) &&
            (buffer->nSize == sizeof (OMX_BUFFERHEADERTYPE) &&
                (buffer->nVersion.nVersion == OMX_SPEC_VERSION)) &&
            (m_inp_bEnabled ==OMX_TRUE)&&
            (search_input_bufhdr(buffer) == true))
    {
        pthread_mutex_lock(&in_buf_count_lock);
        nNumInputBuf++;
        pthread_mutex_unlock(&in_buf_count_lock);
        PrintFrameHdr(OMX_COMPONENT_GENERATE_ETB,buffer);
        post_input((unsigned)hComp,
        (unsigned) buffer,OMX_COMPONENT_GENERATE_ETB);
    }
    else
    {
        DEBUG_PRINT_ERROR("Bad header %x \n", (int)buffer);
        eRet = OMX_ErrorBadParameter;
        if ( m_inp_bEnabled ==OMX_FALSE )
        {
            DEBUG_PRINT("ETB OMX_ErrorIncorrectStateOperation Port \
                Status %d\n", m_inp_bEnabled);
            eRet =  OMX_ErrorIncorrectStateOperation;
        }
        else if (buffer->nVersion.nVersion != OMX_SPEC_VERSION)
        {
            eRet = OMX_ErrorVersionMismatch;
        }

        else if (buffer->nInputPortIndex != 0)
        {
            eRet = OMX_ErrorBadPortIndex;
        }
    }
    return eRet;
}

/**
@brief member function that writes data to kernel driver

@param hComp handle to component instance
@param buffer pointer to buffer header
@return error status
*/
OMX_ERRORTYPE  COmxAacDec::empty_this_buffer_proxy(OMX_IN OMX_HANDLETYPE hComp,
                                    OMX_BUFFERHEADERTYPE* buffer)
{
    int rc = 0;
    OMX_U8 *tmpbuffer = NULL;
    META_IN *pmeta_in = NULL;
    OMX_BUFFERHEADERTYPE *bufHdr = buffer;
    struct msm_audio_aio_buf audio_aio_buf;
    struct msm_audio_buf_cfg buf_cfg;
    unsigned int new_length = buffer->nFilledLen;
    struct aac_header aac_header_info;
    bool flg = false;
    OMX_AUDIO_PARAM_AACPROFILETYPE  m_adec_param_tmp;
    memcpy(&m_adec_param_tmp, &m_adec_param, sizeof(m_adec_param));


    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if(!bufHdr)
    {
        DEBUG_PRINT("UseBufhdr for buffer[%p] is NULL", buffer);
        post_input((unsigned) &m_cmp,(unsigned) bufHdr,
        OMX_COMPONENT_GENERATE_BUFFER_DONE);
        return OMX_ErrorBadParameter;
    }

    //As component in invalid state
    if(m_state == OMX_StateInvalid) {
            DEBUG_PRINT_ERROR("In invalid state..ignore all buffers\n");
            bufHdr->nFilledLen = 0;
            buffer_done_cb((OMX_BUFFERHEADERTYPE *)bufHdr, true);
            return OMX_ErrorInvalidState;
    }

    if((get_eos_bm()) & IP_PORT_BITMASK)
    {
        DEBUG_PRINT("\n ETBP: Input EOS Reached!! Invalid ETB call\n");
        bufHdr->nFilledLen = 0;
        bufHdr->nFlags &= ~OMX_BUFFERFLAG_EOS;
        post_input((unsigned) &m_cmp,(unsigned) bufHdr,
        OMX_COMPONENT_GENERATE_BUFFER_DONE);
        return OMX_ErrorNone;
    }

    if(m_in_use_buf_case)
    {
        DEBUG_PRINT("\nUseBuffer flag SET\n");
        bufHdr = m_loc_in_use_buf_hdrs.find(buffer);
        if(bufHdr == NULL)
            return OMX_ErrorInvalidState;
        if(new_length)
        {
            memcpy(bufHdr->pBuffer, buffer->pBuffer, new_length);
        }
        tmpbuffer = bufHdr->pBuffer;
        memcpy(bufHdr, buffer, sizeof(OMX_BUFFERHEADERTYPE));
        bufHdr->pBuffer = tmpbuffer;
    }

    if(m_first_aac_header == 0)
    {
        struct msm_audio_config drv_config;
        rc = aac_frameheader_parser((OMX_BUFFERHEADERTYPE*)buffer,
                                    &aac_header_info);
        if (rc == 0)
        {
            m_first_aac_header = 1;
            if(ioctl(get_drv_fd(), AUDIO_GET_CONFIG, &drv_config) == -1)
                DEBUG_PRINT("ETBP:get-config ioctl failed %d\n",errno);
            else
                DEBUG_PRINT("ETBP: Get config success\n");
            if(aac_header_info.input_format == FORMAT_ADIF)
            {
                // sampling frequency
                drv_config.sample_rate = aac_header_info.head.adif.sample_rate;
                drv_config.channel_count =
                aac_header_info.head.adif.channel_config;
                m_adec_param_tmp.nSampleRate = aac_header_info.head.adif.sample_rate;
                m_adec_param_tmp.nChannels = aac_header_info.head.adif.channel_config;
                m_adec_param_tmp.eAACStreamFormat = OMX_AUDIO_AACStreamFormatADIF;
//                adif = true;
                DEBUG_PRINT("ETBP-->ADIF SF=%u ch=%u\n",\
                (unsigned int)m_adec_param_tmp.nSampleRate,
                (unsigned int)m_adec_param_tmp.nChannels);
            }
            else if(aac_header_info.input_format == FORMAT_ADTS)
            {
                // sampling frequency
                drv_config.sample_rate =
                aac_frequency_index[aac_header_info.head.adts.fixed.\
                sampling_frequency_index];
                DEBUG_PRINT("ETB-->ADTS format, SF index=%u\n",\
                (unsigned int)aac_header_info.head.adts.fixed.
                sampling_frequency_index);
                drv_config.channel_count =
                aac_header_info.head.adts.fixed.channel_configuration;
                m_adec_param_tmp.nSampleRate =
                aac_frequency_index[
                aac_header_info.head.adts.fixed.sampling_frequency_index];
                m_adec_param_tmp.nChannels =
                    aac_header_info.head.adts.fixed.channel_configuration;
                m_adec_param_tmp.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4ADTS;
                DEBUG_PRINT("ETBP-->ADTS SF=%u ch=%u\n",\
                (unsigned int)m_adec_param_tmp.nSampleRate,
                (unsigned int)m_adec_param_tmp.nChannels);
            }
            else if(aac_header_info.input_format == FORMAT_LOAS)
            {
                m_adec_param_tmp.eAACStreamFormat = OMX_AUDIO_AACStreamFormatMP4LOAS;
                drv_config.sample_rate   = aac_frequency_index[
                aac_header_info.head.loas.freq_index];
                drv_config.channel_count =
                aac_header_info.head.loas.channel_config;
                m_adec_param_tmp.nSampleRate = aac_frequency_index[
                aac_header_info.head.loas.freq_index];
                m_adec_param_tmp.nChannels = aac_header_info.head.loas.channel_config;
                DEBUG_PRINT("ETBP-->LOAS SF=%u ch=%u\n",\
                (unsigned int)m_adec_param_tmp.nSampleRate,
                (unsigned int)m_adec_param_tmp.nChannels);
            }
            else
            {
                DEBUG_PRINT("ETBP-->RAW AAC FORMAT\n");
                // sampling frequency
                drv_config.sample_rate =
                aac_frequency_index[aac_header_info.head.raw.freq_index];
                drv_config.channel_count =
                aac_header_info.head.raw.channel_config;
                m_adec_param_tmp.nSampleRate =
                aac_frequency_index[aac_header_info.head.raw.freq_index];
                m_adec_param_tmp.nChannels = aac_header_info.head.raw.channel_config;
                DEBUG_PRINT("sample_rate=%d ch_cnt=%d sample_rate1=%d\n",\
                drv_config.sample_rate,drv_config.channel_count, aac_header_info.head.raw.ext_freq_index);
                flg = true;
                m_adec_param_tmp.eAACStreamFormat = OMX_AUDIO_AACStreamFormatRAW;
            }
            if(pcm_feedback)
            {
                drv_config.meta_field = 1;
            }
            else
            {
                drv_config.meta_field = 0;
            }
            drv_config.buffer_size = input_buffer_size;
            drv_config.buffer_count = m_inp_act_buf_count;
            drv_config.type = 2; //aac
            drv_config.channel_count = m_adec_param_tmp.nChannels;

            DEBUG_PRINT("o/p config sampling rate = %d channel count = %d",
                drv_config.sample_rate,drv_config.channel_count);

            if(drv_config.channel_count == 0) {
                //over channel count if channel count comes as 2
                drv_config.channel_count = 2;
                m_adec_param_tmp.nChannels = 2;
                DEBUG_PRINT("forcing channel count to 2");
            }

            if((drv_config.sample_rate <= 0) || (drv_config.sample_rate > 48000)){
                DEBUG_PRINT_ERROR("ERROR!! sampling rate %d unsupported\n",\
                         drv_config.sample_rate);
                m_first_aac_header = 0;
                m_state = OMX_StateInvalid;
                bFlushinprogress = true; // To ensure deinit call wait for flush ack
                DEBUG_PRINT("Post invalid state change command\n");
                if(m_cb.EventHandler)
                    m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                        OMX_EventError, OMX_ErrorInvalidState,
                        OMX_COMPONENT_GENERATE_EVENT, NULL );
                DEBUG_PRINT("Post flush on both i/p & o/p\n");
                post_command(OMX_CommandFlush, -1, OMX_COMPONENT_GENERATE_COMMAND);
                DEBUG_PRINT("Post buffer done as no AUDIO_START success\n");
                buffer_done_cb((OMX_BUFFERHEADERTYPE *)bufHdr);
                return OMX_ErrorInvalidState;
            }


            if(ioctl(get_drv_fd(), AUDIO_SET_CONFIG, &drv_config) == -1)
                DEBUG_PRINT("ETBP:set-config ioctl failed %d\n",errno);
            else
                DEBUG_PRINT("ETBP: set config success\n");

            // set aac config
            set_aac_config(&m_adec_param_tmp);
            rc = ioctl(get_drv_fd(), AUDIO_GET_BUF_CFG, &buf_cfg);
            if(rc < 0)
            {
                DEBUG_PRINT_ERROR("ETBP: get buf cfg failed %d", errno);
            } else {
                //TODO
                DEBUG_PRINT("Default meta_info_enable = 0x%8x\n", buf_cfg.meta_info_enable);
                DEBUG_PRINT("Default frames_per_buf = 0x%8x\n", buf_cfg.frames_per_buf);
                // NT mode support meta info
                buf_cfg.meta_info_enable = 1;
                ioctl(get_drv_fd(), AUDIO_SET_BUF_CFG, &buf_cfg);
            }

            rc = ioctl(m_drv_fd, AUDIO_START, 0);
            if(rc <0)
            {
                DEBUG_PRINT_ERROR("AUDIO_START FAILED for Handle %p with error \
                    no.:%d\n", hComp, errno);
                m_first_aac_header= 0;
                //Move to invalid state immediately and report event error to client
                m_state = OMX_StateInvalid;
                bFlushinprogress = true; // To ensure deinit call wait for flush ack
                DEBUG_PRINT_ERROR("Post invalid state change command\n");
                if(m_cb.EventHandler)
                    m_cb.EventHandler(&this->m_cmp, this->m_app_data,
                       OMX_EventError, OMX_ErrorInvalidState,
                       OMX_COMPONENT_GENERATE_EVENT, NULL );
                DEBUG_PRINT_ERROR("Post flush on both i/p & o/p\n");
                post_command(OMX_CommandFlush, -1, OMX_COMPONENT_GENERATE_COMMAND);
                DEBUG_PRINT_ERROR("Post buffer done as no AUDIO_START success\n");
                buffer_done_cb((OMX_BUFFERHEADERTYPE *)bufHdr);
                return OMX_ErrorInvalidState;
            }
            else
            {
                bOutputPortReEnabled = 1;
                DEBUG_PRINT("AUDIO_START SUCCESS for Handle: %p\n", hComp);
                pthread_mutex_lock(&m_out_th_lock_1);
                if ( is_out_th_sleep )
                {
                   is_out_th_sleep = false;
                   DEBUG_DETAIL("ETBP:WAKING UP OUT THREADS\n");
                   out_th_wakeup();
                }
                pthread_mutex_unlock(&m_out_th_lock_1);
                pthread_mutex_lock(&m_in_th_lock_1);
                if(is_in_th_sleep)
                {
                   DEBUG_PRINT("ETBP:WAKING UP IN THREADS\n");
                   in_th_wakeup();
                   is_in_th_sleep = false;
                }
                pthread_mutex_unlock(&m_in_th_lock_1);
            }
        } //end if(rc)
        else
        {
            DEBUG_PRINT("configure Driver for AAC playback samplerate[%u]\n",\
            (unsigned int)m_adec_param_tmp.nSampleRate);
            if(ioctl(get_drv_fd(), AUDIO_GET_CONFIG, &drv_config) == -1)
            DEBUG_PRINT("ETBP:get-config ioctl failed %d\n",errno);
            drv_config.sample_rate = m_adec_param_tmp.nSampleRate;
            drv_config.channel_count = m_adec_param_tmp.nChannels;
            drv_config.type = 2; // aac decoding
            drv_config.buffer_size = input_buffer_size;
            drv_config.buffer_count = m_inp_act_buf_count;

            if(drv_config.channel_count == 0) {
                //force channel count to 2 when there is an error
                drv_config.channel_count = 2;
                m_adec_param_tmp.nChannels = 2;
                DEBUG_PRINT("forcing channel count to 2");
            }
            if(ioctl(get_drv_fd(), AUDIO_SET_CONFIG, &drv_config) == -1)
            DEBUG_PRINT("ETBP:set-config ioctl failed %d\n",errno);
            set_aac_config(&m_adec_param_tmp);
            if(ioctl(get_drv_fd(), AUDIO_START, 0) < 0)
            {
                DEBUG_PRINT("ETBP:audio start ioctl failed %d\n",errno);
                m_first_aac_header=0;
                post_command((unsigned)OMX_CommandStateSet,
                            (unsigned)OMX_StateInvalid,
                            OMX_COMPONENT_GENERATE_COMMAND);
                post_command(OMX_CommandFlush,-1,
                             OMX_COMPONENT_GENERATE_COMMAND);
                buffer_done_cb((OMX_BUFFERHEADERTYPE *)buffer);
                return OMX_ErrorInvalidState;
            }
        }
        DEBUG_PRINT("ETBP: nNumInputBuf %d\n",nNumInputBuf);
        DEBUG_PRINT("ETBP: First aac header \n");
        if (flg == true)
        {
            DEBUG_PRINT("ETBP: Send buffer back to client as it is raw\n");
            buffer_done_cb((OMX_BUFFERHEADERTYPE *)bufHdr);
            return OMX_ErrorNone;
        }
    }// end if (first)

    if (pcm_feedback)//Non-Tunnelled mode
    {
        pmeta_in = (META_IN*) (bufHdr->pBuffer - sizeof(META_IN));
        if(pmeta_in)
        {
            // copy the metadata info from the BufHdr and insert to payload
            pmeta_in->offsetVal = sizeof(META_IN);
            pmeta_in->nTimeStamp.LowPart =
                ((((OMX_BUFFERHEADERTYPE*)bufHdr)->nTimeStamp)& 0xFFFFFFFF);
            pmeta_in->nTimeStamp.HighPart =
                (((((OMX_BUFFERHEADERTYPE*)bufHdr)->nTimeStamp) >> 32)
                 & 0xFFFFFFFF);
            pmeta_in->nFlags = bufHdr->nFlags;
            DEBUG_PRINT("ETB Timestamp: %x msw %x lsw %x\n",(unsigned int)bufHdr->nTimeStamp,
                        (unsigned int)pmeta_in->nTimeStamp.HighPart,
                        (unsigned int)pmeta_in->nTimeStamp.LowPart);
        }
        else
        {
            DEBUG_PRINT_ERROR("\n Invalid pmeta_in(NULL)\n");
            buffer_done_cb((OMX_BUFFERHEADERTYPE *)bufHdr);
            return OMX_ErrorUndefined;
        }
    }
    // EOS support with dependancy on Kernel and DSP
    if( (bufHdr->nFlags & OMX_BUFFERFLAG_EOS))
    {
        if(new_length == 0)
        {
            set_eos_bm(IP_PORT_BITMASK);
            DEBUG_PRINT("EOS set in input buffer received\n");
        }
        else
        {
            if(pcm_feedback)
            {
                pmeta_in->nFlags &= ~OMX_BUFFERFLAG_EOS;
            }
        }
    }
    /* For tunneled case, EOS buffer with 0 length not pushed */
    if(!pcm_feedback && (bufHdr->nFlags & OMX_BUFFERFLAG_EOS)
            && new_length == 0)
    {
        buffer_done_cb((OMX_BUFFERHEADERTYPE *)bufHdr);
        DEBUG_PRINT("\nETBP: EOS with 0 length in Tunneled Mode\n");
    }
    else
    {
        DEBUG_PRINT("ETBP: Before write nFlags[%u] len[%d]\n",
            (unsigned) bufHdr->nFlags, new_length);

        /* Asynchronous write call to the AAC driver */
        audio_aio_buf.buf_len = bufHdr->nAllocLen;
        audio_aio_buf.private_data = bufHdr;

        if(pcm_feedback)
        {
            audio_aio_buf.buf_addr = (OMX_U8*)pmeta_in;
            audio_aio_buf.data_len = new_length + sizeof(META_IN);
            audio_aio_buf.mfield_sz = sizeof(META_IN);
        }
        else
        {
            audio_aio_buf.data_len = new_length;
            audio_aio_buf.buf_addr = bufHdr->pBuffer;
        }

        DEBUG_PRINT ("\nETBP: Sending Data buffer[%x], Filled length = %d\n",
        (unsigned)( audio_aio_buf.buf_addr), audio_aio_buf.data_len);

        if(m_to_idle)
        {
            buffer_done_cb((OMX_BUFFERHEADERTYPE *)bufHdr);
            return OMX_ErrorNone;
        }
        pthread_mutex_lock(&in_buf_count_lock);
        drv_inp_buf_cnt++;
        DEBUG_PRINT("\n i/p Buffers with AAC drv = %d\n", drv_inp_buf_cnt);
        pthread_mutex_unlock(&in_buf_count_lock);

        rc = aio_write(&audio_aio_buf);
        if(rc == false)
        {
            pthread_mutex_lock(&in_buf_count_lock);
            drv_inp_buf_cnt--;
            DEBUG_PRINT("ETBP: nNumInputBuf %d, drv_inp_buf_cnt %d\n",
                nNumInputBuf, drv_inp_buf_cnt);
            pthread_mutex_unlock(&in_buf_count_lock);
            buffer_done_cb((OMX_BUFFERHEADERTYPE *)bufHdr);
            return OMX_ErrorUndefined;
        }
        DEBUG_PRINT("\n AUDIO_ASYNC_WRITE issued for %x %x\n",
            (unsigned)bufHdr->pBuffer,(unsigned int)bufHdr->nFilledLen);
    }
    return OMX_ErrorNone;
}

OMX_ERRORTYPE  COmxAacDec::fill_this_buffer_proxy (
        OMX_IN OMX_HANDLETYPE         hComp,
                OMX_BUFFERHEADERTYPE* buffer)
{
    int ret = 0;
    META_OUT *pmeta_out = NULL;
    msm_audio_aio_buf audio_aio_buf;
    OMX_BUFFERHEADERTYPE *bufHdr = buffer;

    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if(m_out_use_buf_case)
    {
        bufHdr = m_loc_out_use_buf_hdrs.find(buffer);

        if(!bufHdr)
        {
            DEBUG_PRINT("UseBufhdr for buffer[%p] is NULL", buffer);
            return OMX_ErrorBadParameter;
        }
    }
    /*if((is_trigger_eos() == OMX_TRUE))
    {
        // HACK--> as we cant issue O/p flush now.
        bufHdr->nFlags= 0x01;
        bufHdr->nFilledLen = 0;
        bufHdr->nTimeStamp= get_comp()->getTS();
        trigger_eos(OMX_FALSE);
        post_output((unsigned) & hComp,(unsigned) bufHdr,OMX_COMPONENT_GENERATE_FRAME_DONE);

        return OMX_ErrorNone;
    }*/
    if((search_output_bufhdr(buffer) == true))
    {
        pmeta_out = (META_OUT*) (bufHdr->pBuffer - sizeof(META_OUT));
        if(!pmeta_out)
        {
            DEBUG_PRINT_ERROR("\n Invalid pmeta_out(NULL)\n");
            return OMX_ErrorUndefined;
        }

        DEBUG_PRINT("\n Calling ASYNC_READ for %u bytes \n",
            (unsigned)(bufHdr->nAllocLen));
        audio_aio_buf.buf_len = bufHdr->nAllocLen;
        audio_aio_buf.private_data = bufHdr;
        audio_aio_buf.buf_addr = (OMX_U8*)pmeta_out;
        audio_aio_buf.data_len = 0;
        audio_aio_buf.mfield_sz = sizeof(META_OUT);

        pthread_mutex_lock(&out_buf_count_lock);
        drv_out_buf_cnt++;
        DEBUG_PRINT("\n o/p Buffers with AAC drv = %d\n", drv_out_buf_cnt);
        pthread_mutex_unlock(&out_buf_count_lock);

        ret = aio_read(&audio_aio_buf);
        if(ret == false)
        {
            DEBUG_PRINT("\n Error in ASYNC READ call, ret = %d\n", ret);
            pthread_mutex_lock(&out_buf_count_lock);
            drv_out_buf_cnt--;
            nNumOutputBuf--;
            DEBUG_PRINT("FTBP: nNumOutputBuf %d, drv_out_buf_cnt %d\n",
                nNumOutputBuf, drv_out_buf_cnt);
            pthread_mutex_unlock(&out_buf_count_lock);
            return OMX_ErrorUndefined;
        }
        DEBUG_PRINT("\n AUDIO_ASYNC_READ issued for %x\n",
            (unsigned)(audio_aio_buf.buf_addr));
    }
    else
    {
        DEBUG_PRINT("\n FTBP:Invalid buffer in FTB \n");
        pthread_mutex_lock(&out_buf_count_lock);
        nNumOutputBuf--;
        pthread_mutex_unlock(&out_buf_count_lock);
        return OMX_ErrorUndefined;
    }
    return OMX_ErrorNone;

}


/* ======================================================================
FUNCTION
COmxAacDec::FillThisBuffer

DESCRIPTION
IL client uses this method to release the frame buffer
after displaying them.

PARAMETERS
None.

RETURN VALUE
true/false

========================================================================== */
OMX_ERRORTYPE  COmxAacDec::fill_this_buffer(OMX_IN OMX_HANDLETYPE hComp,
                                    OMX_IN OMX_BUFFERHEADERTYPE* buffer)
{
    OMX_STATETYPE state;
    OMX_ERRORTYPE eRet = OMX_ErrorNone;

    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);

    if (buffer == NULL)
	    return OMX_ErrorBadParameter;

    pthread_mutex_lock(&m_state_lock);
    get_state(&m_cmp, &state);
    pthread_mutex_unlock(&m_state_lock);
    if ( state == OMX_StateInvalid )
    {
        DEBUG_PRINT("Fill this buffer in Invalid State\n");
        return OMX_ErrorInvalidState;
    }

    if( (buffer->nOutputPortIndex == 1) &&
            (buffer->nSize == sizeof (OMX_BUFFERHEADERTYPE)) &&
            (buffer->nVersion.nVersion == OMX_SPEC_VERSION) &&
            (search_output_bufhdr(buffer) == true)&&(m_out_bEnabled==OMX_TRUE)
            )
    {
        pthread_mutex_lock(&out_buf_count_lock);
        m_ftb_cnt++;
        nNumOutputBuf++;
        DEBUG_PRINT("FTB: nNumOutputBuf %d ftb_cnt %d bufferhdr %u",
            nNumOutputBuf, m_ftb_cnt, (unsigned)buffer);
        pthread_mutex_unlock(&out_buf_count_lock);
        PrintFrameHdr(OMX_COMPONENT_GENERATE_FTB,buffer);
        post_output((unsigned)hComp,
        (unsigned) buffer,OMX_COMPONENT_GENERATE_FTB);
    }
    else
    {
        eRet = OMX_ErrorBadParameter;
        if (m_out_bEnabled == OMX_FALSE)
        {
            eRet=OMX_ErrorIncorrectStateOperation;
        }
        else if (buffer->nVersion.nVersion != OMX_SPEC_VERSION)
        {
            eRet = OMX_ErrorVersionMismatch;
        }
        else if (buffer->nOutputPortIndex != 1)
        {
            eRet = OMX_ErrorBadPortIndex;
        }
    }
    return eRet;
}

/* ======================================================================
FUNCTION
COmxAacDec::SetCallbacks

DESCRIPTION
Set the callbacks.

PARAMETERS
None.

RETURN VALUE
OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  COmxAacDec::set_callbacks(OMX_IN OMX_HANDLETYPE hComp,
                                    OMX_IN OMX_CALLBACKTYPE* callbacks,
                                    OMX_IN OMX_PTR             appData)
{
    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    m_cb       = *callbacks;
    m_app_data =    appData;

    return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
COmxAacDec::ComponentDeInit

DESCRIPTION
Destroys the component and release memory allocated to the heap.

PARAMETERS
<TBD>.

RETURN VALUE
OMX Error None if everything successful.

========================================================================== */
OMX_ERRORTYPE  COmxAacDec::component_deinit(OMX_IN OMX_HANDLETYPE hComp)
{
#if DUMPS_ENABLE
    if (bfd != -1)
        close(bfd);
    if (pfd != -1)
        close(pfd);
#endif
    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if (OMX_StateLoaded != m_state)
    {
        DEBUG_PRINT_ERROR("Warning: rxed deInit, not in LOADED state[%d]",
            m_state);
    DEBUG_PRINT("Wait for flush to complete if flush is in progress\n");
    if (bFlushinprogress)
        wait_for_flush_event();
    DEBUG_PRINT("Flush completed\n");
    }
    deinit_decoder();
    DEBUG_PRINT_ERROR("%s:COMPONENT DEINIT...\n", __FUNCTION__);
    return OMX_ErrorNone;
}

/* ======================================================================
FUNCTION
COmxAacDec::deinit_decoder

DESCRIPTION
Closes all the threads and release memory allocated to the heap.

PARAMETERS
None.

RETURN VALUE
None.

========================================================================== */
void  COmxAacDec::deinit_decoder()
{
    DEBUG_PRINT("Component-deinit STARTED\n");

    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if(OMX_StateInvalid == m_state){
        unsigned total_pending = 0;
        pthread_mutex_lock(&m_outputlock);
        DEBUG_PRINT("output: flush %d fbd %d ftb %d \n",
			 m_cout_q.m_size, m_fbd_q.m_size, m_dout_q.m_size);
        total_pending = m_fbd_q.m_size;
        total_pending += m_dout_q.m_size;
        pthread_mutex_unlock(&m_outputlock);
        pthread_mutex_lock(&m_inputlock);
        DEBUG_PRINT("input: flush %d ebd %d etb %d \n",
			 m_cin_q.m_size, m_ebd_q.m_size, m_din_q.m_size);
        total_pending += m_ebd_q.m_size;
        total_pending += m_din_q.m_size;
        pthread_mutex_unlock(&m_inputlock);

        pthread_mutex_lock(&m_commandlock);
        DEBUG_PRINT("command: cmd %d\n",  m_cmd_q.m_size);
        pthread_mutex_unlock(&m_commandlock);
        if(total_pending) {
            post_command(OMX_CommandFlush, -1, OMX_COMPONENT_GENERATE_COMMAND);
            DEBUG_PRINT("insleep %d outsleep %d\n",  is_in_th_sleep, is_out_th_sleep);
            in_th_wakeup();
            out_th_wakeup();
            DEBUG_PRINT("wait for flush completion\n");
            wait_for_flush_event();
        }
    }
    pthread_mutex_lock(&m_in_th_lock_1);
    if(is_in_th_sleep)
    {
        DEBUG_DETAIL("Deinit:WAKING UP IN THREADS\n");
        in_th_wakeup();
        is_in_th_sleep = false;
    }
    pthread_mutex_unlock(&m_in_th_lock_1);

    pthread_mutex_lock(&m_out_th_lock_1);
    if(is_out_th_sleep)
    {
        DEBUG_DETAIL("Deinit:WAKING UP OUT THREADS\n");
        out_th_wakeup();
        is_out_th_sleep = false;
    }
    pthread_mutex_unlock(&m_out_th_lock_1);

    DEBUG_PRINT("Component-deinit calling omx_aac_thread_stop\
            (m_ipc_to_in_th)\n");
    if (m_ipc_to_in_th != NULL) {
        omx_aac_thread_stop(m_ipc_to_in_th);
        m_ipc_to_in_th = NULL;
    }

    DEBUG_PRINT("Component-deinit calling omx_aac_thread_stop\
            (m_ipc_to_cmd_th)\n");

    if (m_ipc_to_cmd_th != NULL) {
        omx_aac_thread_stop(m_ipc_to_cmd_th);
        m_ipc_to_cmd_th = NULL;
    }

    drv_inp_buf_cnt = 0;
    drv_out_buf_cnt = 0;
    m_to_idle = false;
    DEBUG_PRINT("Component-deinit success ..\n");
    m_is_alloc_buf = 0;

    if (m_drv_fd >= 0)
    {
        if(ioctl(m_drv_fd, AUDIO_STOP, 0) <0)
        DEBUG_PRINT("De-init: AUDIO_STOP FAILED\n");
        if(ioctl(m_drv_fd,AUDIO_ABORT_GET_EVENT,NULL) < 0)
        {
            DEBUG_PRINT("De-init: AUDIO_ABORT_GET_EVENT FAILED\n");
        }
        DEBUG_PRINT("De-init: Free buffer\n");
        if(close(m_drv_fd) < 0)
        DEBUG_PRINT("De-init: Driver Close Failed \n");
        m_drv_fd = -1;
    }
    else
    {
        DEBUG_PRINT_ERROR(" aac device already closed\n");
    }

    if(ionfd >= 0)
        close(ionfd);

    DEBUG_PRINT("Component-deinit calling omx_aac_thread_stop\
            (m_ipc_to_event_th)\n");
    if (m_ipc_to_event_th != NULL) {
        omx_aac_thread_stop(m_ipc_to_event_th);
        m_ipc_to_event_th = NULL;
    }

    DEBUG_PRINT("Component-deinit pcm_feedback = %d\n",pcm_feedback);

    if(pcm_feedback ==1)
    {
        DEBUG_PRINT("Component-deinit calling omx_aac_thread_stop\
            (m_ipc_to_out_th)\n");
        if (m_ipc_to_out_th != NULL) {
            omx_aac_thread_stop(m_ipc_to_out_th);
            m_ipc_to_out_th = NULL;
        }
    }

    m_to_idle =false;
    bOutputPortReEnabled = 0;
    bFlushinprogress = 0;
    nNumInputBuf = 0;
    nNumOutputBuf = 0;
    suspensionPolicy= OMX_SuspensionDisabled;
    is_in_th_sleep = false;
    is_out_th_sleep = false;
    m_first_aac_header = 0;
    m_inp_current_buf_count=0;
    m_out_current_buf_count=0;
    m_inp_bEnabled = OMX_FALSE;
    m_out_bEnabled = OMX_FALSE;
    m_inp_bPopulated = OMX_FALSE;
    m_out_bPopulated = OMX_FALSE;
    input_buffer_size = 0;
    output_buffer_size = 0;
    m_flush_cmpl_event = 0;
    set_eos_bm(0);
    m_comp_deinit = 1;
    DEBUG_PRINT("Component-deinit m_drv_fd = %d\n",m_drv_fd);
    DEBUG_PRINT("Component-deinit end \n");
}

/* ======================================================================
FUNCTION
COmxAacDec::UseEGLImage

DESCRIPTION
OMX Use EGL Image method implementation <TBD>.

PARAMETERS
<TBD>.

RETURN VALUE
Not Implemented error.

========================================================================== */
OMX_ERRORTYPE  COmxAacDec::use_EGL_image(OMX_IN OMX_HANDLETYPE hComp,
                                    OMX_INOUT OMX_BUFFERHEADERTYPE** bufferHdr,
                                    OMX_IN OMX_U32                        port,
                                    OMX_IN OMX_PTR                     appData,
                                    OMX_IN void*                      eglImage)
{
    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    (void)bufferHdr;
    (void)port;
    (void)appData;
    (void)eglImage;

    DEBUG_PRINT_ERROR("Error : use_EGL_image:  Not Implemented \n");
    return OMX_ErrorNotImplemented;
}

/* ======================================================================
FUNCTION
COmxAacDec::ComponentRoleEnum

DESCRIPTION
OMX Component Role Enum method implementation.

PARAMETERS
<TBD>.

RETURN VALUE
OMX Error None if everything is successful.
========================================================================== */
OMX_ERRORTYPE  COmxAacDec::component_role_enum(OMX_IN OMX_HANDLETYPE hComp,
                                    OMX_OUT OMX_U8*        role,
                                    OMX_IN OMX_U32        index)
{
    OMX_ERRORTYPE eRet = OMX_ErrorNone;
    const char *cmp_role = "audio_decoder.aac";

    /* To remove warning for unused variable to keep prototype same */
    (void)hComp;
    if(index == 0 && role)
    {
        memcpy(role, cmp_role, sizeof(cmp_role));
        *(((char *) role) + sizeof(cmp_role)) = '\0';
    }
    else
    {
        eRet = OMX_ErrorNoMore;
    }
    return eRet;
}


/* ======================================================================
FUNCTION
COmxAacDec::AllocateDone

DESCRIPTION
Checks if entire buffer pool is allocated by IL Client or not.
Need this to move to IDLE state.

PARAMETERS
None.

RETURN VALUE
true/false.

========================================================================== */
bool COmxAacDec::allocate_done(void)
{
    OMX_BOOL bRet = OMX_FALSE;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if(pcm_feedback==1)
    {
        if ((m_inp_act_buf_count == m_inp_current_buf_count)
                &&(m_out_act_buf_count == m_out_current_buf_count))
        {
            bRet=OMX_TRUE;
        }
        if((m_inp_act_buf_count == m_inp_current_buf_count) && m_inp_bEnabled)
        m_inp_bPopulated = OMX_TRUE;

        if((m_out_act_buf_count == m_out_current_buf_count) && m_out_bEnabled)
        m_out_bPopulated = OMX_TRUE;
    }
    else if(pcm_feedback==0)
    {
        if(m_inp_act_buf_count == m_inp_current_buf_count)
        {
            bRet=OMX_TRUE;
        }
        if((m_inp_act_buf_count == m_inp_current_buf_count) && m_inp_bEnabled)
        m_inp_bPopulated = OMX_TRUE;
    }
    return bRet;
}


/* ======================================================================
FUNCTION
COmxAacDec::ReleaseDone

DESCRIPTION
Checks if IL client has released all the buffers.

PARAMETERS
None.

RETURN VALUE
true/false

========================================================================== */
bool COmxAacDec::release_done(OMX_U32 param1)
{
    OMX_BOOL bRet = OMX_FALSE;
    DEBUG_PRINT("%s %p\n",__FUNCTION__,this);
    if(param1 == OMX_ALL)
    {
        if ((0 == m_inp_current_buf_count)&&(0 == m_out_current_buf_count))
        {
            bRet=OMX_TRUE;
        }
    }
    else if(param1 == 0 )
    {
        if ((0 == m_inp_current_buf_count))
        {
            bRet=OMX_TRUE;
        }
    }
    else if(param1 ==1)
    {
        if ((0 == m_out_current_buf_count))
        {
            bRet=OMX_TRUE;
        }
    }
    return bRet;
}

OMX_ERRORTYPE COmxAacDec::set_aac_config(OMX_AUDIO_PARAM_AACPROFILETYPE  *m_adec_param_tmp)
{
    DEBUG_PRINT("COmxAacDec::%s\n",__FUNCTION__);
    struct msm_audio_aac_config aac_config;

    DEBUG_PRINT("Set-aac-config: Ch=%u SF=%u profile=%d stream=%d chmode=%d\n",
    (unsigned int)m_adec_param_tmp->nChannels,
    (unsigned int)m_adec_param_tmp->nSampleRate,
    m_adec_param_tmp->eAACProfile,m_adec_param_tmp->eAACStreamFormat,
    m_adec_param_tmp->eChannelMode);

    if (ioctl(m_drv_fd, AUDIO_GET_AAC_CONFIG, &aac_config)) {
        DEBUG_PRINT("omx_aac_adec::set_aac_config():GET_AAC_CONFIG failed");
        m_first_aac_header=0;
        if(m_drv_fd >= 0){
            post_command((unsigned)OMX_CommandStateSet,
                        (unsigned)OMX_StateInvalid,
                        OMX_COMPONENT_GENERATE_COMMAND);
        }
        return OMX_ErrorInvalidComponent;
    }
    else
    DEBUG_PRINT("ETBP: get aac config success\n");

    DEBUG_PRINT("AAC-CFG::format[%d]chcfg[%d]audobj[%d]epcfg[%d]data_res[%d]\n",
    aac_config.format,
    aac_config.channel_configuration,
    aac_config.audio_object,
    aac_config.ep_config,
    aac_config.aac_section_data_resilience_flag);

    DEBUG_PRINT("CFG:scalefact[%d]spectral[%d]sbron[%d]sbrps[%d]dualmono[%d]",\
    aac_config.aac_scalefactor_data_resilience_flag,
    aac_config.aac_spectral_data_resilience_flag,
    aac_config.sbr_on_flag,
    aac_config.sbr_ps_on_flag,
    aac_config.dual_mono_mode);

    switch(m_adec_param_tmp->eAACStreamFormat)
    {
    case OMX_AUDIO_AACStreamFormatMP4LOAS:
        aac_config.format = AUDIO_AAC_FORMAT_LOAS;
        break;
    case OMX_AUDIO_AACStreamFormatMP4ADTS:
        aac_config.format = AUDIO_AAC_FORMAT_ADTS;
        break;
    case OMX_AUDIO_AACStreamFormatADIF:
        aac_config.format = AUDIO_AAC_FORMAT_ADIF;
        break;
    case OMX_AUDIO_AACStreamFormatRAW:
    default:
        aac_config.format = AUDIO_AAC_FORMAT_PSUEDO_RAW ;
        break;
    }

    switch(m_adec_param_tmp->eAACProfile)
    {
    case  OMX_AUDIO_AACObjectLC:
    default:
        aac_config.sbr_on_flag = 0;
        aac_config.sbr_ps_on_flag = 0;
        break;
    case  OMX_AUDIO_AACObjectHE:
        aac_config.sbr_on_flag = 1;
        aac_config.sbr_ps_on_flag = 1;
        break;
    case  OMX_AUDIO_AACObjectHE_PS:
        aac_config.sbr_on_flag = 1;
        aac_config.sbr_ps_on_flag = 1;
        break;
    }

    aac_config.channel_configuration = m_adec_param_tmp->nChannels;
    if ((m_adec_config_dualmono.eChannelConfig < OMX_AUDIO_DUAL_MONO_MODE_FL_FR) ||
       (m_adec_config_dualmono.eChannelConfig > OMX_AUDIO_DUAL_MONO_MODE_FL_SR)){
        aac_config.dual_mono_mode = OMX_AUDIO_DUAL_MONO_MODE_INVALID;
    } else
        aac_config.dual_mono_mode = m_adec_config_dualmono.eChannelConfig;

    DEBUG_PRINT("COmxAacDec: dual_mono_mode: %d\n", aac_config.dual_mono_mode);
    if (ioctl(m_drv_fd, AUDIO_SET_AAC_CONFIG, &aac_config)) {
        DEBUG_PRINT("set_aac_config():AUDIO_SET_AAC_CONFIG failed");
        m_first_aac_header=0;
        post_command((unsigned)OMX_CommandStateSet,
                    (unsigned)OMX_StateInvalid,
                    OMX_COMPONENT_GENERATE_COMMAND);
        return OMX_ErrorInvalidComponent;
    }
    else
    DEBUG_PRINT("COmxAacDec: set aac config success\n");
    return OMX_ErrorNone;
}

OMX_ERRORTYPE COmxAacDec::aac_frameheader_parser(
                                    OMX_BUFFERHEADERTYPE* bufHdr,
				    struct aac_header *header)
{
    DEBUG_PRINT("COmxAacDec::%s\n",__FUNCTION__);
    OMX_U8 *buf = bufHdr->pBuffer;
    OMX_U32 ext_aud_obj_type = 0;

    if( (buf[0] == 0x41) &&  (buf[1] == 0x44) &&
            (buf[2] == 0x49) &&  (buf[3] == 0x46) )
    /*check "ADIF" */
    {
        // format is ADIF
        // parser to parse ADIF
        header->input_format = FORMAT_ADIF;
        audaac_extract_adif_header(bufHdr->pBuffer,header);
    }

    else
    {
        DEBUG_PRINT("Parser-->RAW or Partial frame to be found...\n");
        OMX_U8 *data;
        OMX_U32 temp, ext_flag, status = 0, i = 0;
        OMX_U8 sync_word_found = 0;
        while(i < (bufHdr->nFilledLen))
        {
            if ((buf[i] == 0xFF) && ((buf[i+1] & 0xF6) == 0xF0) )
            {
                OMX_U32 index=0;OMX_U32 sf=0;
                if((i+3) >= (bufHdr->nFilledLen))
                {
                    break;
                }
                sf = (buf[i+2] & 0x3C) >> 2;
                // Get the frequency index from bits 19,20,21,22
                //Get the channel configuration bits 24,25,26
                index = (buf[i+2] & 0x01) << 0x02 ;
                index |= (buf[i+3] & 0xC0) >> 0x06;
                DEBUG_PRINT("PARSER-->Partial frame..ADTS sync word \n");
                DEBUG_PRINT("ADTS-->freq_index=%u ch-cfg=%u\n",\
                (unsigned int)sf,(unsigned int)index);

                //if not ok continue, false sync words found
                if((sf > 12) || (index > 6))
                {
                    DEBUG_PRINT("Parser-->fake sync words sf=%u index=%u\n",\
                    (unsigned int)sf,(unsigned int)index);
                    i++;
                    // invalid SF/ch-cfg, maybe fake sync word found
                    continue;
                }
                sync_word_found = 1;
                m_bytes_to_skip = i;
                DEBUG_PRINT("ADTS-->bytes to skip : %d\n", m_bytes_to_skip);
                /* TBD: Need to be revisited
                 */
                if (index == 0)
                    DEBUG_PRINT("ADTS-->channels(index =0 case): %d\n", index);

                header->input_format = FORMAT_ADTS;
                header->head.adts.fixed.channel_configuration = index;
                header->head.adts.fixed.sampling_frequency_index = sf;
                break;
            }
            else if ((buf[i] == 0x56) && ((buf[i+1] & 0xE0) == 0xE0) )
            {
                DEBUG_PRINT("PARSER-->PARTIAL..LOAS\n");
                unsigned int ret = 0;
                //DEBUG_PRINT("i=%u buf[i]=%u buf[i+1] %u\n",i,buf[i], buf[i+1]);

                ret = audaac_extract_loas_header(
                &bufHdr->pBuffer[i],
                (((bufHdr->nFilledLen)-i)*8),header);
                if((int)ret == -1)
                {
                    DEBUG_PRINT("Parser--> fake sync words SF=%u cf=%u\n",\
                    (unsigned int)header->head.loas.freq_index,\
                    (unsigned int)header->head.loas.channel_config);
                    i++;
                    // invalid SF/ch-cfg, maybe fake sync word found
                    continue;
                }
                sync_word_found = 1;
                m_bytes_to_skip = i;
                DEBUG_PRINT("LOAS-->bytes to skip : %d\n", m_bytes_to_skip);

                header->input_format = FORMAT_LOAS;

                break;
            }
            i++;
        }
        if(!sync_word_found)
        {
            // RAW AAC-ADTS PARSER
            header->input_format = FORMAT_RAW;

            m_aac_hdr_bit_index = 0;
            data = (OMX_U8*)buf;

            audaac_extract_bits(data, 5, &temp);
            header->head.raw.aud_obj_type = temp;
            header->head.raw.ext_aud_obj_type = temp;
            DEBUG_DETAIL("RAW-->aud_obj_type=0x%x\n",\
                                                 header->head.raw.aud_obj_type);
            if(header->head.raw.ext_aud_obj_type == 22)
            {
                //m_bsac = OMX_TRUE;/*BSAC*/
            }
            audaac_extract_bits(data, AAC_SAMPLING_FREQ_INDEX_SIZE, &temp);
            header->head.raw.freq_index = temp;
            header->head.raw.ext_freq_index = temp;
            DEBUG_DETAIL("RAW-->freq_index=0x%x\n",header->head.raw.freq_index);
            if(header->head.raw.freq_index == 0xf)
            {
                /* Current release does not support sampling freqiencies
                 other than frequencies listed in aac_frequency_index[] */
                /* Extract sampling frequency value */
                audaac_extract_bits(data, 12, &temp);
                audaac_extract_bits(data, 12, &temp);
            }
            /* Extract channel config value */
            audaac_extract_bits(data, 4, &temp);
            header->head.raw.channel_config = temp;
            DEBUG_DETAIL("RAW-->ch cfg=0x%x\n",header->head.raw.channel_config);
            header->head.raw.sbr_present_flag = 0;
            header->head.raw.sbr_ps_present_flag = 0;
            /* If the audioOjectType is SBR or PS */
            if((header->head.raw.aud_obj_type == 5) ||
                    (header->head.raw.aud_obj_type == 29))
            {
                header->head.raw.ext_aud_obj_type = 5;
                header->head.raw.sbr_present_flag = 1;
                if (header->head.raw.aud_obj_type == 29)
                {
                    header->head.raw.ext_aud_obj_type = 29;
                    header->head.raw.sbr_ps_present_flag = 1;
                }
                /* extensionSamplingFrequencyIndex */
                audaac_extract_bits(data, 4, &temp);
                header->head.raw.ext_freq_index = temp;

                if (header->head.raw.ext_freq_index == 0x0f)
                {
                    /* Extract sampling frequency value */
                    audaac_extract_bits(data, 12, &temp);
                    audaac_extract_bits(data, 12, &temp);
                }
                audaac_extract_bits(data, 5, &temp);
                header->head.raw.aud_obj_type = temp;
            }
            /* If audioObjectType is AAC_LC or AAC_LTP */
            if (header->head.raw.aud_obj_type == 2 ||
                    header->head.raw.aud_obj_type == 4)
            {
                /* frame_length_flag : 0 - 1024; 1 - 960
        Current release supports AAC frame length of 1024 only */
                audaac_extract_bits(data, 1, &temp);
                if(temp) status = 1;
                /* dependsOnCoreCoder == 1, core coder has different sampling rate
        in a scalable bitstream. This is mainly set fro AAC_SCALABLE Object only.
        Currrent release does not support AAC_SCALABLE Object */
                audaac_extract_bits(data, 1, &temp);
                if(temp) status = 1;

                /* extensionFlag */
                audaac_extract_bits(data, 1, &ext_flag);

                if(header->head.raw.channel_config == 0 && status == 0)
                {
                    OMX_U32 num_fce, num_bce, num_sce, num_lfe, num_ade, num_vce;
                    OMX_U32 i, profile, samp_freq_idx, num_ch = 0;

                    /* Parse Program configuration element information */
                    audaac_extract_bits(data, AAC_ELEMENT_INSTANCE_TAG_SIZE, &temp);
                    audaac_extract_bits(data, AAC_PROFILE_SIZE, &profile);
                    DEBUG_DETAIL("RAW-->PCE -- profile=%lu\n",profile);
                    audaac_extract_bits(data,
                    AAC_SAMPLING_FREQ_INDEX_SIZE,
                    &samp_freq_idx);
                    DEBUG_DETAIL("RAW-->PCE -- samp_freq_idx=%lu\n",samp_freq_idx);
                    audaac_extract_bits(data, AAC_NUM_FRONT_CHANNEL_ELEMENTS_SIZE,
                    &num_fce);
                    DEBUG_DETAIL("RAW-->PCE -- num_fce=%lu\n",num_fce);

                    audaac_extract_bits(data, AAC_NUM_SIDE_CHANNEL_ELEMENTS_SIZE,
                    &num_sce);
                    DEBUG_DETAIL("RAW-->PCE -- num_sce=%lu\n",num_sce);
                    audaac_extract_bits(data, AAC_NUM_BACK_CHANNEL_ELEMENTS_SIZE,
                    &num_bce);
                    DEBUG_DETAIL("RAW-->PCE -- num_bce=%lu\n",num_bce);

                    audaac_extract_bits(data, AAC_NUM_LFE_CHANNEL_ELEMENTS_SIZE,
                    &num_lfe);
                    DEBUG_DETAIL("RAW-->PCE -- num_lfe=%lu\n",num_lfe);

                    audaac_extract_bits(data, AAC_NUM_ASSOC_DATA_ELEMENTS_SIZE,
                    &num_ade);
                    DEBUG_DETAIL("RAW-->PCE -- num_ade=%lu\n",num_ade);

                    audaac_extract_bits(data, AAC_NUM_VALID_CC_ELEMENTS_SIZE,
                    &num_vce);
                    DEBUG_DETAIL("RAW-->PCE -- num_vce=%lu\n",num_vce);

                    audaac_extract_bits(data, AAC_MONO_MIXDOWN_PRESENT_SIZE,
                    &temp);
                    if(temp) {
                        audaac_extract_bits(data, AAC_MONO_MIXDOWN_ELEMENT_SIZE,
                        &temp);
                    }

                    audaac_extract_bits(data, AAC_STEREO_MIXDOWN_PRESENT_SIZE,
                    &temp);
                    if(temp) {
                        audaac_extract_bits(data, AAC_STEREO_MIXDOWN_ELEMENT_SIZE,
                        &temp);
                    }

                    audaac_extract_bits(data, AAC_MATRIX_MIXDOWN_PRESENT_SIZE,
                    &temp);
                    if(temp) {
                        audaac_extract_bits(data, AAC_MATRIX_MIXDOWN_SIZE, &temp);
                    }

                    for(i=0; i<(num_fce+num_sce+num_bce); i++) {
                        /* Is Element CPE ?*/
                        audaac_extract_bits(data, 1, &temp);
                        /* If the element is CPE, increment num channels by 2 */
                        if(temp) num_ch += 2;
                        else     num_ch += 1;
                        DEBUG_DETAIL("RAW-->PCE -- num_ch=%lu\n",num_ch);
                        audaac_extract_bits(data, 4, &temp);
                    }
                    for(i=0; i<num_lfe; i++) {
                        /* LFE element can not be CPE */
                        num_ch += 1;
                        DEBUG_DETAIL("RAW-->PCE -- num_ch=%lu\n",num_ch);
                        audaac_extract_bits(data, AAC_LFE_SIZE, &temp);
                    }
                    for(i=0; i<num_ade; i++) {
                        audaac_extract_bits(data, AAC_ADE_SIZE, &temp);
                    }
                    for(i=0; i<num_vce; i++) {
                        /* Coupling channels can not be counted as output
                         channels as they will be
                         coupled with main audio channels */
                        audaac_extract_bits(data, AAC_VCE_SIZE, &temp);
                    }
                    header->head.raw.channel_config = num_ch ;
                    /* byte_alignment() */
                    temp = (OMX_U8)(m_aac_hdr_bit_index % 8);
                    if(temp) {
                        m_aac_hdr_bit_index += 8 - temp;
                    }
                    /* get comment_field_bytes */
                    temp = data[m_aac_hdr_bit_index/8];
                    m_aac_hdr_bit_index += AAC_COMMENT_FIELD_BYTES_SIZE;
                    /* Skip the comment */
                    m_aac_hdr_bit_index += temp * AAC_COMMENT_FIELD_DATA_SIZE;
                    /* byte_alignment() */
                    temp = (OMX_U8)(m_aac_hdr_bit_index % 8);
                    if(temp) {
                        m_aac_hdr_bit_index += 8 - temp;
                    }
                    DEBUG_DETAIL("RAW-->PCE -- m_aac_hdr_bit_index=0x%x\n",\
                    m_aac_hdr_bit_index);
                }
                if (ext_flag)
                {
                    /* 17: ER_AAC_LC 19:ER_AAC_LTP
                    20:ER_AAC_SCALABLE 23:ER_AAC_LD */
                    if (header->head.raw.aud_obj_type == 17 ||
                            header->head.raw.aud_obj_type == 19 ||
                            header->head.raw.aud_obj_type == 20 ||
                            header->head.raw.aud_obj_type == 23)
                    {
                        audaac_extract_bits(data, 1, &temp);
                        audaac_extract_bits(data, 1, &temp);
                        audaac_extract_bits(data, 1, &temp);
                    }

                    /* extensionFlag3 */
                    audaac_extract_bits(data, 1, &temp);
                    if(temp) status = 1;
                }
            }

            /* SBR tool explicit signaling ( backward compatible ) */
            if (ext_aud_obj_type != 5)
            {
                /* syncExtensionType */
                audaac_extract_bits(data, 11, &temp);
                if (temp == 0x2b7)
                {
                    /*extensionAudioObjectType*/
                    audaac_extract_bits(data, 5, &ext_aud_obj_type);
                    if(ext_aud_obj_type == 5)
                    {
                        /*sbrPresentFlag*/
                        audaac_extract_bits(data, 1, &temp);
                        header->head.raw.sbr_present_flag = temp;
                        if(temp)
                        {
                            /*extensionSamplingFrequencyIndex*/
                            audaac_extract_bits(data,
                            AAC_SAMPLING_FREQ_INDEX_SIZE,
                            &temp);
                            header->head.raw.ext_freq_index = temp;
                            if(temp == 0xf)
                            {
                                /*Extract 24 bits for sampling frequency value */
                                audaac_extract_bits(data, 12, &temp);
                                audaac_extract_bits(data, 12, &temp);
                            }

                            /* syncExtensionType */
                            audaac_extract_bits(data, 11, &temp);

                            if (temp == 0x548)
                            {
                                /*psPresentFlag*/
                                audaac_extract_bits(data, 1, &temp);
                                header->head.raw.sbr_ps_present_flag = temp;
                                if(temp)
                                {
                                    /* extAudioObject is PS */
                                    ext_aud_obj_type = 29;
                                }
                            }
                        }
                    }
                    header->head.raw.ext_aud_obj_type = ext_aud_obj_type;
                }
            }
            DEBUG_PRINT("RAW-->ch cfg=0x%x\n",\
            header->head.raw.channel_config);
            DEBUG_PRINT("RAW-->freq_index=0x%x\n",\
            header->head.raw.freq_index);
            DEBUG_PRINT("RAW-->ext_freq_index=0x%x\n",\
            header->head.raw.ext_freq_index);
            DEBUG_PRINT("RAW-->aud_obj_type=0x%x\n",\
            header->head.raw.aud_obj_type);
            DEBUG_PRINT("RAW-->ext_aud_obj_type=0x%x\n",\
            header->head.raw.ext_aud_obj_type);
        }
    }
    return OMX_ErrorNone;
}

void COmxAacDec::audaac_extract_adif_header(
                                OMX_U8 *data,
                                struct aac_header *aac_header_info)
{
    OMX_U32  buf8 = 0;
    OMX_U32 buf32 = 0;
    OMX_U32  num_pfe = 0, num_fce = 0, num_sce = 0,
	     num_bce = 0, num_lfe = 0, num_ade = 0, num_vce = 0;
    OMX_U32  pfe_index = 0;
    OMX_U8  i = 0;
    OMX_U32 temp = 0;
    OMX_U32 isCpe = 0,totalChannels = 0 ;
    DEBUG_PRINT("COmxAacDec::%s\n",__FUNCTION__);

    /* We already parsed the 'ADIF' keyword. Skip it. */
    m_aac_hdr_bit_index = 32;

    audaac_extract_bits(data, AAC_COPYRIGHT_PRESENT_SIZE, &buf8);

    if(buf8) {
        /* Copyright present; Just discard it for now */
        m_aac_hdr_bit_index += 72;
    }

    audaac_extract_bits(data, AAC_ORIGINAL_COPY_SIZE,
    &temp);

    audaac_extract_bits(data, AAC_HOME_SIZE, &temp);

    audaac_extract_bits(data, AAC_BITSTREAM_TYPE_SIZE,
    &temp);
    aac_header_info->head.adif.variable_bit_rate = temp;

    audaac_extract_bits(data, AAC_BITRATE_SIZE, &temp);

    audaac_extract_bits(data, AAC_NUM_PFE_SIZE, &num_pfe);

    for(pfe_index=0; pfe_index<num_pfe+1; pfe_index++) {
        if(!aac_header_info->head.adif.variable_bit_rate) {
            /* Discard */
            audaac_extract_bits(data, AAC_BUFFER_FULLNESS_SIZE, &buf32);
        }

        /* Extract Program Config Element */

        /* Discard element instance tag */
        audaac_extract_bits(data, AAC_ELEMENT_INSTANCE_TAG_SIZE, &buf8);

        audaac_extract_bits(data, AAC_PROFILE_SIZE,
        &buf8);
        aac_header_info->head.adif.aud_obj_type = buf8;

        buf8 = 0;
        audaac_extract_bits(data, AAC_SAMPLING_FREQ_INDEX_SIZE, &buf8);
        aac_header_info->head.adif.freq_index = buf8;
        aac_header_info->head.adif.sample_rate =
                                       aac_frequency_index[buf8];

        audaac_extract_bits(data, AAC_NUM_FRONT_CHANNEL_ELEMENTS_SIZE, &num_fce);

        audaac_extract_bits(data, AAC_NUM_SIDE_CHANNEL_ELEMENTS_SIZE, &num_sce);

        audaac_extract_bits(data, AAC_NUM_BACK_CHANNEL_ELEMENTS_SIZE, &num_bce);

        audaac_extract_bits(data, AAC_NUM_LFE_CHANNEL_ELEMENTS_SIZE, &num_lfe);

        audaac_extract_bits(data, AAC_NUM_ASSOC_DATA_ELEMENTS_SIZE, &num_ade);

        audaac_extract_bits(data, AAC_NUM_VALID_CC_ELEMENTS_SIZE, &num_vce);

        audaac_extract_bits(data, AAC_MONO_MIXDOWN_PRESENT_SIZE, &buf8);
        if(buf8) {
            audaac_extract_bits(data, AAC_MONO_MIXDOWN_ELEMENT_SIZE, &buf8);
        }

        audaac_extract_bits(data, AAC_STEREO_MIXDOWN_PRESENT_SIZE, &buf8);
        if(buf8) {
            audaac_extract_bits(data, AAC_STEREO_MIXDOWN_ELEMENT_SIZE, &buf8);
        }

        audaac_extract_bits(data, AAC_MATRIX_MIXDOWN_PRESENT_SIZE, &buf8);
        if(buf8) {
            audaac_extract_bits(data, AAC_MATRIX_MIXDOWN_SIZE, &buf8);
        }

        for(i=0; i<num_fce; i++) {
            audaac_extract_bits(data, 1, &isCpe);
            totalChannels += (isCpe ? 2 : 1);
            audaac_extract_bits(data, (AAC_FCE_SIZE - 1), &buf8);
        }

        for(i=0; i<num_sce; i++) {
            audaac_extract_bits(data, 1, &isCpe);
            totalChannels += (isCpe ? 2 : 1);
            audaac_extract_bits(data, (AAC_SCE_SIZE -1), &buf8);
        }

        for(i=0; i<num_bce; i++) {
            audaac_extract_bits(data, 1, &isCpe);
            totalChannels += (isCpe ? 2 : 1);
            audaac_extract_bits(data, (AAC_BCE_SIZE - 1), &buf8);
        }

        for(i=0; i<num_lfe; i++) {
            audaac_extract_bits(data, 1, &isCpe);
            totalChannels += (isCpe ? 2 : 1);
            audaac_extract_bits(data, (AAC_LFE_SIZE - 1), &buf8);
        }

        for(i=0; i<num_ade; i++) {
            audaac_extract_bits(data, 1, &isCpe);
            totalChannels += (isCpe ? 2 : 1);
            audaac_extract_bits(data, (AAC_ADE_SIZE - 1), &buf8);
        }

        for(i=0; i<num_vce; i++) {
            audaac_extract_bits(data, 1, &isCpe);
            totalChannels += (isCpe ? 2 : 1);
            audaac_extract_bits(data, (AAC_VCE_SIZE - 1), &buf8);
        }

        /* byte_alignment() */
        buf8 = (OMX_U8)(m_aac_hdr_bit_index % 8);
        if(buf8) {
            m_aac_hdr_bit_index += 8 - buf8;
        }

        /* get comment_field_bytes */
        buf8 = data[m_aac_hdr_bit_index/8];

        m_aac_hdr_bit_index += AAC_COMMENT_FIELD_BYTES_SIZE;

        /* Skip the comment */
        m_aac_hdr_bit_index += buf8 * AAC_COMMENT_FIELD_DATA_SIZE;
    }

    /* byte_alignment() */
    buf8 = (OMX_U8)(m_aac_hdr_bit_index % 8);
    if(buf8) {
        m_aac_hdr_bit_index += 8 - buf8;
    }

    aac_header_info->head.adif.channel_config = totalChannels;
}

int COmxAacDec::audaac_extract_loas_header(
                                OMX_U8 *data,OMX_U32 len,
                                struct aac_header *aac_header_info)
{
    DEBUG_PRINT("COmxAacDec::%s\n",__FUNCTION__);
    OMX_U32      aac_frame_length = 0;
    OMX_U32      value    = 0;
    OMX_U32       obj_type = 0;
    OMX_U32       ext_flag = 0;

    OMX_U32       num_fc   = 0;
    OMX_U32       num_sc   = 0;
    OMX_U32       num_bc   = 0;
    OMX_U32       num_lfe  = 0;
    OMX_U32       num_ade  = 0;
    OMX_U32       num_vce  = 0;

    OMX_U16      num_bits_to_skip = 0;
    DEBUG_PRINT("LOAS HEADER -->len=%u\n",(unsigned int)len);
    /* Length is in the bits 11 to 23 from left in the bitstream */
    m_aac_hdr_bit_index = 11;
    if(((m_aac_hdr_bit_index+42)) > len)
    return -1;

    audaac_extract_bits(data, 13, &aac_frame_length);

    /* useSameStreamMux */
    audaac_extract_bits(data, 1, &value);

    if(value)
    {
        return -1;
    }
    /* Audio mux version */
    audaac_extract_bits(data, 1, &value);
    if (value)
    {
        /* Unsupported format */
        return -1;
    }
    /* allStreamsSameTimeFraming */
    audaac_extract_bits(data, 1, &value);
    if (!value)
    {
        /* Unsupported format */
        return -1;
    }
    /* numSubFrames */
    audaac_extract_bits(data, 6, &value);
    if (value)
    {
        /* Unsupported format */
        return -1;
    }
    /* numProgram */
    audaac_extract_bits(data, 4, &value);
    if (value)
    {
        /* Unsupported format */
        return -1;
    }
    /* numLayer */
    audaac_extract_bits(data, 3, &value);
    if (value)
    {
        /* Unsupported format */
        return -1;
    }

    /* Audio specific config */
    /* audioObjectType */
    audaac_extract_bits(data, 5, &value);
    if (!LOAS_IS_AUD_OBJ_SUPPORTED(value))
    {
        /* Unsupported format */
        return -1;
    }
    aac_header_info->head.loas.aud_obj_type = value;

    /* SFI */
    audaac_extract_bits(data, 4, &value);
    if (!LOAS_IS_SFI_SUPPORTED(value))
    {
        /* Unsupported format */
        return -1;
    }
    aac_header_info->head.loas.freq_index = value;

    /* Channel config */
    audaac_extract_bits(data, 4, &value);
    if (!LOAS_IS_CHANNEL_CONFIG_SUPPORTED(value,
                aac_header_info->head.loas.aud_obj_type))
    {
        /* Unsupported format */
        return -1;
    }

    aac_header_info->head.loas.channel_config = value;

    if (aac_header_info->head.loas.aud_obj_type == 5)
    {
        /* Extension SFI */
        if((m_aac_hdr_bit_index+9) > len)
        return -1;
        audaac_extract_bits(data, 4, &value);
        if (!LOAS_IS_EXT_SFI_SUPPORTED(value))
        {
            /* Unsupported format */
            return -1;
        }
        /* Audio object_type */
        audaac_extract_bits(data, 5, &value);
        if (!LOAS_IS_AUD_OBJ_SUPPORTED(value))
        {
            /* Unsupported format */
            return -1;
        }
        aac_header_info->head.loas.aud_obj_type = value;
    }
    obj_type = aac_header_info->head.loas.aud_obj_type;

    if (LOAS_GA_SPECIFIC_CONFIG(obj_type))
    {
        if((m_aac_hdr_bit_index+3) > len)
        return -1;
        /* framelengthFlag */
        audaac_extract_bits(data, 1, &value);
        if (value)
        {
            /* Unsupported format */
            return -1;
        }

        /* dependsOnCoreCoder */
        audaac_extract_bits(data, 1, &value);
        if (value)
        {
            /* Unsupported format */
            return -1;
        }

        /* extensionFlag */
        audaac_extract_bits(data, 1, &ext_flag);
        if (!LOAS_IS_EXT_FLG_SUPPORTED(ext_flag,obj_type) )
        {
            /* Unsupported format */
            return -1;
        }
        if (!aac_header_info->head.loas.channel_config)
        {
            /* Skip 10 bits */
            if((m_aac_hdr_bit_index+45) > len)
            return -1;
            audaac_extract_bits(data, 10, &value);

            audaac_extract_bits(data, 4, &num_fc);
            audaac_extract_bits(data, 4, &num_sc);
            audaac_extract_bits(data, 4, &num_bc);
            audaac_extract_bits(data, 2, &num_lfe);
            audaac_extract_bits(data, 3, &num_ade);
            audaac_extract_bits(data, 4, &num_vce);

            /* mono_mixdown_present */
            audaac_extract_bits(data, 1, &value);
            if (value) {
                /* mono_mixdown_element_number */
                audaac_extract_bits(data, 4, &value);
            }

            /* stereo_mixdown_present */
            audaac_extract_bits(data, 1, &value);
            if (value) {
                /* stereo_mixdown_element_number */
                audaac_extract_bits(data, 4, &value);
            }

            /* matrix_mixdown_idx_present */
            audaac_extract_bits(data, 1, &value);
            if (value) {
                /* matrix_mixdown_idx and presudo_surround_enable */
                audaac_extract_bits(data, 3, &value);
            }
            num_bits_to_skip = (num_fc * 5) + (num_sc * 5) + (num_bc * 5) +
            (num_lfe * 4) + (num_ade * 4) + (num_vce * 5);

            if((m_aac_hdr_bit_index+num_bits_to_skip) > len)
            return -1;

            while (num_bits_to_skip != 0){
                if (num_bits_to_skip > 32) {
                    audaac_extract_bits(data, 32, &value);
                    num_bits_to_skip -= 32;
                } else {
                    audaac_extract_bits(data, num_bits_to_skip, &value);
                    num_bits_to_skip = 0;
                }
            }

            if (m_aac_hdr_bit_index & 0x07)
            {
                m_aac_hdr_bit_index +=  (8 - (m_aac_hdr_bit_index & 0x07));
            }
            if((m_aac_hdr_bit_index + 8) > len)
            return -1;
            /* comment_field_bytes */
            audaac_extract_bits(data, 8, &value);

            num_bits_to_skip = value * 8;

            if((m_aac_hdr_bit_index + num_bits_to_skip) > len)
            return -1;

            while (num_bits_to_skip != 0){
                if (num_bits_to_skip > 32) {
                    audaac_extract_bits(data, 32, &value);
                    num_bits_to_skip -= 32;
                } else {
                    audaac_extract_bits(data, num_bits_to_skip, &value);
                    num_bits_to_skip = 0;
                }
            }
        }

        if (ext_flag)
        {
            if (((obj_type == 17) || (obj_type == 19) ||
                        (obj_type == 20) || (obj_type == 23)))
            {
                if((m_aac_hdr_bit_index + 3) > len)
                return -1;
                audaac_extract_bits(data, 1, &value);
                audaac_extract_bits(data, 1, &value);
                audaac_extract_bits(data, 1, &value);
            }
            /* extensionFlag3 */
            if((m_aac_hdr_bit_index + 1) > len)
            return -1;
            audaac_extract_bits(data, 1, &value);
        }
    }
    if ((obj_type != 18) && (obj_type >= 17) && (obj_type <= 27))
    {
        /* epConfig */
        if((m_aac_hdr_bit_index + 2) > len)
        return -1;
        audaac_extract_bits(data, 2, &value);
        if (value)
        {
            /* Unsupported format */
            return -1;
        }
    }
    /* Back in StreamMuxConfig */
    /* framelengthType */
    if((m_aac_hdr_bit_index + 3) > len)
    return -1;
    audaac_extract_bits(data, 3, &value);
    if (value)
    {
        /* Unsupported format */
        return -1;
    }

    if((aac_header_info->head.loas.freq_index > 12) ||
            (aac_header_info->head.loas.channel_config > 2))
    {
        return -1;
    }
    else
    return 0;
}

void COmxAacDec::audaac_extract_bits(
                                OMX_U8  *input,
                                OMX_U8  num_bits_reqd,
                                OMX_U32 *out_buf
)
{
    DEBUG_PRINT("COmxAacDec::%s\n",__FUNCTION__);
    OMX_U32 output = 0;
    OMX_U32 value = 0;
    OMX_U32 byte_index;
    OMX_U8   bit_index;
    OMX_U8   bits_avail_in_byte;
    OMX_U8   num_to_copy;
    OMX_U8   mask;
    OMX_U8   num_remaining = num_bits_reqd;

    while(num_remaining) {
        byte_index = m_aac_hdr_bit_index / 8;
        bit_index  = m_aac_hdr_bit_index % 8;

        bits_avail_in_byte = 8 - bit_index;
        num_to_copy = MIN(bits_avail_in_byte, num_remaining);

        mask = ~(0xff << bits_avail_in_byte);

        value = input[byte_index] & mask;
        value = value >> (bits_avail_in_byte - num_to_copy);

        m_aac_hdr_bit_index += num_to_copy;
        num_remaining -= num_to_copy;

        output = (output << num_to_copy) | value;
    }
    *out_buf=output;

}
